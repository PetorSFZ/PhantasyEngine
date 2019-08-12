// Copyright (c) Peter Hillerström (skipifzero.com, peter@hstroem.se)
//               For other contributors see Contributors.txt
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include "ph/renderer/DynamicGpuAllocator.hpp"

#include <mutex>
#include <utility> // std::swap()

#include <sfz/Assert.hpp>
#include <sfz/containers/DynArray.hpp>
#include <sfz/containers/HashMap.hpp>

#include "ph/renderer/ZeroGUtils.hpp"

namespace ph {

using sfz::DynArray;

// Constants
// ------------------------------------------------------------------------------------------------

constexpr uint32_t BUFFER_ALIGNMENT = 65536; // 64 KiB
constexpr uint32_t TEXTURE_ALIGNMENT = 65536; // 64 KiB
constexpr uint32_t PAGE_SIZE_DEVICE = 64 * 1024 * 1024; // 64 MiB
constexpr uint32_t PAGE_SIZE_UPLOAD = 32 * 1024 * 1024; // 32 MiB
constexpr uint32_t PAGE_SIZE_TEXTURE = 64 * 1024 * 1024; // 64 MiB
constexpr uint32_t MAX_NUM_PAGES = 256;
constexpr uint32_t MAX_NUM_BLOCKS_PER_PAGE = PAGE_SIZE_DEVICE / BUFFER_ALIGNMENT;

static_assert((PAGE_SIZE_DEVICE % BUFFER_ALIGNMENT) == 0, "Unaligned device page size");
static_assert((PAGE_SIZE_UPLOAD % BUFFER_ALIGNMENT) == 0, "Unaligned upload page size");
static_assert((PAGE_SIZE_TEXTURE % TEXTURE_ALIGNMENT) == 0, "Unaligned texture page size");

// Private datatypes
// ------------------------------------------------------------------------------------------------

struct Block final {
	uint32_t offset = ~0u;
	uint32_t size = 0;
};

struct MemoryPage final {
	zg::MemoryHeap heap;
	DynArray<Block> freeBlocks;
	uint32_t pageSize = 0;
	uint32_t numAllocations = 0;
	uint32_t largestFreeBlockSize = 0;
};

struct TexturePage final {
	zg::TextureHeap heap;
	DynArray<Block> freeBlocks;
	uint32_t pageSize = 0;
	uint32_t numAllocations = 0;
	uint32_t largestFreeBlockSize = 0;
};

struct AllocEntryBuffer final {
	Block block;
	DynArray<MemoryPage>* pages = nullptr;
	void* heapPtr = nullptr; // Used as unique identifier to find page again
};

struct AllocEntryTexture final {
	Block block;
	void* heapPtr = nullptr; // Used as unique identifier to find page again
};

struct DynamicGpuAllocatorState final {

	std::mutex mutex;
	sfz::Allocator* allocator = nullptr;

	DynArray<MemoryPage> devicePages;
	DynArray<MemoryPage> uploadPages;
	DynArray<TexturePage> texturePages;

	sfz::HashMap<ZgBuffer*, AllocEntryBuffer> bufferEntries;
	sfz::HashMap<ZgTexture2D*, AllocEntryTexture> textureEntries;
	
	uint32_t totalNumAllocationsDevice = 0;
	uint32_t totalNumAllocationsUpload = 0;
	uint32_t totalNumAllocationsTexture = 0;

	uint32_t totalNumDeallocationsDevice = 0;
	uint32_t totalNumDeallocationsUpload = 0;
	uint32_t totalNumDeallocationsTexture = 0;
};

// Statics
// ------------------------------------------------------------------------------------------------

// Returns whether a new free block was returned or not
static bool calculateNewBlocks(
	const Block& oldFreeBlock,
	uint32_t allocSize,
	uint32_t alignment,
	Block& allocBlockOut,
	Block& newFreeBlockOut) noexcept
{
	sfz_assert_debug((oldFreeBlock.offset % alignment) == 0);
	sfz_assert_debug(oldFreeBlock.size != 0);
	sfz_assert_debug((oldFreeBlock.size % alignment) == 0);

	// Calculate aligned allocation size
	uint32_t leftoverAlignedSize = allocSize % alignment;
	uint32_t alignedAllocSize = allocSize;
	if (leftoverAlignedSize != 0) {
		alignedAllocSize += (alignment - leftoverAlignedSize);
	}
	sfz_assert_debug(allocSize <= alignedAllocSize);
	sfz_assert_debug(alignedAllocSize <= oldFreeBlock.size);
	sfz_assert_debug((alignedAllocSize % alignment) == 0);

	// Create and return allocation block
	allocBlockOut = oldFreeBlock;
	allocBlockOut.size = alignedAllocSize;

	// Calculate and return new free block if space left
	bool createdNewFreeBlock = alignedAllocSize != oldFreeBlock.size;
	if (createdNewFreeBlock) {
		newFreeBlockOut = oldFreeBlock;
		newFreeBlockOut.offset += alignedAllocSize;
		newFreeBlockOut.size -= alignedAllocSize;
	}

	// Return whether new free block was created or not
	return createdNewFreeBlock;
}

static bool createMemoryPage(
	MemoryPage& page, uint32_t size, ZgMemoryType memoryType, sfz::Allocator* allocator) noexcept
{
	sfz_assert_debug(size != 0);
	sfz_assert_debug((size % BUFFER_ALIGNMENT) == 0);
	sfz_assert_debug(!page.heap.valid());
	bool heapAllocSuccess = CHECK_ZG page.heap.create(size, memoryType);
	if (!heapAllocSuccess) return false;

	// Allocate memory for free blocks
	page.freeBlocks.create(MAX_NUM_BLOCKS_PER_PAGE, allocator);

	// Add initial block
	Block initialBlock;
	initialBlock.offset = 0;
	initialBlock.size = size;
	page.freeBlocks.add(initialBlock);

	// Set other info and return true
	page.pageSize = size;
	page.largestFreeBlockSize = size;
	return true;
}

static bool createTexturePage(
	TexturePage& page, uint32_t size, sfz::Allocator* allocator) noexcept
{
	sfz_assert_debug(size != 0);
	sfz_assert_debug((size % TEXTURE_ALIGNMENT) == 0);
	sfz_assert_debug(!page.heap.valid());
	bool heapAllocSuccess = CHECK_ZG page.heap.create(size);
	if (!heapAllocSuccess) return false;

	// Allocate memory for free blocks
	page.freeBlocks.create(MAX_NUM_BLOCKS_PER_PAGE, allocator);

	// Add initial block
	Block initialBlock;
	initialBlock.offset = 0;
	initialBlock.size = size;
	page.freeBlocks.add(initialBlock);

	// Set other info and return true
	page.pageSize = size;
	page.largestFreeBlockSize = size;
	return true;
}

template<typename PageT, typename ItemAllocFunc>
static bool pageAllocateItem(
	PageT& page, uint32_t size, Block& allocBlockOut, ItemAllocFunc itemAllocFunc) noexcept
{
	sfz_assert_debug(size <= page.largestFreeBlockSize);

	// Find first free block big enough
	// TODO: O(n) linear search, consider replacing with binary search
	uint32_t blockIdxToUse = ~0u;
	for (uint32_t i = 0; i < page.freeBlocks.size(); i++) {
		Block& block = page.freeBlocks[i];
		if (block.size >= size) {
			blockIdxToUse = i;
			break;
		}
	}
	if (blockIdxToUse == ~0u) return false;

	// Calculate new blocks
	Block& oldFreeBlock = page.freeBlocks[blockIdxToUse];
	Block newFreeBlock;
	Block allocBlock;
	bool createdNewFreeBlock =
		calculateNewBlocks(oldFreeBlock, size, BUFFER_ALIGNMENT, allocBlock, newFreeBlock);

	// Allocate GPU memory
	bool allocSuccess = itemAllocFunc(page, allocBlock);
	if (!allocSuccess) return false;

	// If a new free block was created, just replace the old one with it
	if (createdNewFreeBlock) {
		oldFreeBlock = newFreeBlock;
	}

	// Otherwise remove old free block completely
	else {
		page.freeBlocks.remove(blockIdxToUse);
	}

	// Find and set new largest free block size
	// TODO: O(n) memory access, consider doing something smarter since we already access most
	//       blocks earlier in this method.
	page.largestFreeBlockSize = 0;
	for (const Block& block : page.freeBlocks) {
		page.largestFreeBlockSize = std::max(page.largestFreeBlockSize, block.size);
	}
	sfz_assert_debug(!(page.freeBlocks.size() != 0 && page.largestFreeBlockSize == 0));
	sfz_assert_debug((page.largestFreeBlockSize % BUFFER_ALIGNMENT) == 0);

	// Increment number of allocation counter
	page.numAllocations += 1;

	// Return allocation block
	allocBlockOut = allocBlock;
	return true; 
}

template<typename PageT>
static void pageDeallocateBlock(PageT& page, Block& allocatedBlock) noexcept
{
	sfz_assert_debug(allocatedBlock.size != 0);
	sfz_assert_debug(allocatedBlock.size <= page.pageSize);
	sfz_assert_debug((allocatedBlock.offset + allocatedBlock.size) <= page.pageSize);
	sfz_assert_debug((allocatedBlock.offset % BUFFER_ALIGNMENT) == 0);
#if !defined(SFZ_NO_DEBUG)
	// Check that no free block overlaps with the allocated block in debug
	{
		uint32_t allocatedBegin = allocatedBlock.offset;
		uint32_t allocatedEnd = allocatedBlock.offset + allocatedBlock.size;
		for (uint32_t i = 0; i < page.freeBlocks.size(); i++) {
			const Block& block = page.freeBlocks[i];
			uint32_t blockBegin = block.offset;
			uint32_t blockEnd = block.offset + block.size;
				
			// TODO: I think using <= will cause edge cases because integers, but I haven't
			//       thought about it too much. Since this is just a sanity check it should be
			//       fine.
			bool overlap = blockBegin < allocatedEnd && blockEnd > allocatedBegin;
			sfz_assert_debug(!overlap);

			// Ensure that blocks are ordered by offset
			if ((i + 1) < page.freeBlocks.size()) {
				sfz_assert_debug(block.offset < page.freeBlocks[i + 1].offset);
			}
		}
	}
#endif

	// Find free block right before this allocated block
	uint32_t freeBlockBeforeIdx = ~0u;
	for (uint32_t i = 0; i < page.freeBlocks.size(); i++) {
		const Block& freeBlock = page.freeBlocks[i];
		if ((freeBlock.offset + freeBlock.size) == allocatedBlock.offset) {
			freeBlockBeforeIdx = i;
			break;
		}
	}

	// Merge allocated block with free block before it
	uint32_t newFreeBlockIdx = ~0u;
	{
		// If free block before found, merge it with allocated block returned
		if (freeBlockBeforeIdx != ~0u) {
			Block& freeBlock = page.freeBlocks[freeBlockBeforeIdx];
			sfz_assert_debug((freeBlock.offset + freeBlock.size) == allocatedBlock.offset);
			freeBlock.size += allocatedBlock.size;
			newFreeBlockIdx = freeBlockBeforeIdx;
		}
		
		// If no free blocks left in page we can just insert allocated block as new free block
		else if (page.freeBlocks.size() == 0) {
			page.freeBlocks.add(allocatedBlock);
			newFreeBlockIdx = 0;
		}

		// Insert block at correct location
		else {

			// Find insertion location
			uint32_t insertLoc = ~0u;
			if (allocatedBlock.offset < page.freeBlocks.first().offset) {
				insertLoc = 0;
			}
			else if (allocatedBlock.offset > page.freeBlocks.last().offset) {
				insertLoc = page.freeBlocks.size() - 1;
			}
			else {
				sfz_assert_debug(page.freeBlocks.size() >= 2);
				for (uint32_t i = 1; i < page.freeBlocks.size(); i++) {
					Block& prev = page.freeBlocks[i - 1];
					Block& curr = page.freeBlocks[i];
					if (prev.offset < allocatedBlock.offset && allocatedBlock.offset < curr.offset) {
						insertLoc = i;
						break;
					}
				}
			}
			sfz_assert_debug(insertLoc != ~0u);

			// Insert block
			page.freeBlocks.insert(insertLoc, allocatedBlock);
			newFreeBlockIdx = insertLoc;
		}
	}

	// Check if free block after the new one should be merged with it
	if ((newFreeBlockIdx + 1) < page.freeBlocks.size()) {
		Block& freeBlock = page.freeBlocks[newFreeBlockIdx];
		Block& nextFreeBlock = page.freeBlocks[newFreeBlockIdx + 1];
		bool shouldMerge = (freeBlock.offset + freeBlock.size) == nextFreeBlock.offset;
		if (shouldMerge) {
			freeBlock.size += nextFreeBlock.size;
			page.freeBlocks.remove(newFreeBlockIdx + 1);
		}
	}

	// Update largest free block size
	Block& newFreeBlock = page.freeBlocks[newFreeBlockIdx];
	page.largestFreeBlockSize = std::max(page.largestFreeBlockSize, newFreeBlock.size);

	// Decrement number of allocation counter
	page.numAllocations -= 1;
}

template<typename PageT>
static uint32_t findAppropriatePage(DynArray<PageT>& pages, uint32_t size) noexcept
{
	sfz_assert_debug(size != 0);
	for (uint32_t i = 0; i < pages.size(); i++) {
		PageT& page = pages[i];
		if (page.pageSize >= size) return i;
	}
	return ~0u;
}

// DynamicGpuAllocator: State methods
// ------------------------------------------------------------------------------------------------

void DynamicGpuAllocator::init(sfz::Allocator* allocator) noexcept
{
	this->destroy();
	mState = allocator->newObject<DynamicGpuAllocatorState>("DynamicGpuAllocatorState");
	mState->allocator = allocator;

	// Allocate memory for page meta data
	mState->devicePages.create(MAX_NUM_PAGES, allocator);
	mState->uploadPages.create(MAX_NUM_PAGES, allocator);

	mState->bufferEntries.create(MAX_NUM_PAGES * MAX_NUM_BLOCKS_PER_PAGE * 4 * 2, allocator);
	mState->textureEntries.create(MAX_NUM_PAGES * MAX_NUM_BLOCKS_PER_PAGE * 4 * 2, allocator);
}

void DynamicGpuAllocator::swap(DynamicGpuAllocator& other) noexcept
{
	std::swap(this->mState, other.mState);
}

void DynamicGpuAllocator::destroy() noexcept
{
	if (mState != nullptr) {
		sfz_assert_debug(mState->bufferEntries.size() == 0);
		sfz_assert_debug(mState->textureEntries.size() == 0);
		sfz_assert_debug(mState->totalNumAllocationsDevice == mState->totalNumDeallocationsDevice);
		sfz_assert_debug(mState->totalNumAllocationsUpload == mState->totalNumDeallocationsUpload);
		sfz_assert_debug(mState->totalNumAllocationsTexture == mState->totalNumDeallocationsTexture);
		for (MemoryPage& page : mState->devicePages) sfz_assert_debug(page.numAllocations == 0);
		for (MemoryPage& page : mState->uploadPages) sfz_assert_debug(page.numAllocations == 0);
		for (TexturePage& page : mState->texturePages) sfz_assert_debug(page.numAllocations == 0);
		sfz::Allocator* allocator = mState->allocator;
		allocator->deleteObject(mState);
	}
	mState = nullptr;
}

// State query methods
// ------------------------------------------------------------------------------------------------

uint32_t DynamicGpuAllocator::queryTotalNumAllocationsDevice() const noexcept
{
	return mState->totalNumAllocationsDevice;
}

uint32_t DynamicGpuAllocator::queryTotalNumAllocationsUpload() const noexcept
{
	return mState->totalNumAllocationsUpload;
}

uint32_t DynamicGpuAllocator::queryTotalNumAllocationsTexture() const noexcept
{
	return mState->totalNumAllocationsTexture;
}

uint32_t DynamicGpuAllocator::queryTotalNumDeallocationsDevice() const noexcept
{
	return mState->totalNumDeallocationsDevice;
}

uint32_t DynamicGpuAllocator::queryTotalNumDeallocationsUpload() const noexcept
{
	return mState->totalNumDeallocationsUpload;
}

uint32_t DynamicGpuAllocator::queryTotalNumDeallocationsTexture() const noexcept
{
	return mState->totalNumDeallocationsTexture;
}

uint32_t DynamicGpuAllocator::queryDefaultPageSizeDevice() const noexcept
{
	return PAGE_SIZE_DEVICE;
}

uint32_t DynamicGpuAllocator::queryDefaultPageSizeUpload() const noexcept
{
	return PAGE_SIZE_UPLOAD;
}

uint32_t DynamicGpuAllocator::queryDefaultPageSizeTexture() const noexcept
{
	return PAGE_SIZE_TEXTURE;
}

uint32_t DynamicGpuAllocator::queryNumPagesDevice() const noexcept
{
	return mState->devicePages.size();
}

uint32_t DynamicGpuAllocator::queryNumPagesUpload() const noexcept
{
	return mState->uploadPages.size();
}

uint32_t DynamicGpuAllocator::queryNumPagesTexture() const noexcept
{
	return mState->texturePages.size();
}

PageInfo DynamicGpuAllocator::queryPageInfoDevice(uint32_t pageIdx) const noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	PageInfo info;
	if (mState->devicePages.size() <= pageIdx) return info;
	MemoryPage& page = mState->devicePages[pageIdx];
	info.pageSizeBytes = page.pageSize;
	info.numAllocations = page.numAllocations;
	info.numFreeBlocks = page.freeBlocks.size();
	info.largestFreeBlockBytes = page.largestFreeBlockSize;
	return info;
}

PageInfo DynamicGpuAllocator::queryPageInfoUpload(uint32_t pageIdx) const noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	PageInfo info;
	if (mState->uploadPages.size() <= pageIdx) return info;
	MemoryPage& page = mState->uploadPages[pageIdx];
	info.pageSizeBytes = page.pageSize;
	info.numAllocations = page.numAllocations;
	info.numFreeBlocks = page.freeBlocks.size();
	info.largestFreeBlockBytes = page.largestFreeBlockSize;
	return info;
}

PageInfo DynamicGpuAllocator::queryPageInfoTexture(uint32_t pageIdx) const noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	PageInfo info;
	if (mState->texturePages.size() <= pageIdx) return info;
	TexturePage& page = mState->texturePages[pageIdx];
	info.pageSizeBytes = page.pageSize;
	info.numAllocations = page.numAllocations;
	info.numFreeBlocks = page.freeBlocks.size();
	info.largestFreeBlockBytes = page.largestFreeBlockSize;
	return info;
}

// Allocation methods
// ------------------------------------------------------------------------------------------------

zg::Buffer DynamicGpuAllocator::allocateBuffer(ZgMemoryType memoryType, uint32_t sizeBytes) noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	sfz_assert_debug(memoryType == ZG_MEMORY_TYPE_DEVICE || memoryType == ZG_MEMORY_TYPE_UPLOAD);

	// Get array of pages depending on memory type
	DynArray<MemoryPage>* pages = nullptr;
	if (memoryType == ZG_MEMORY_TYPE_DEVICE) pages = &mState->devicePages;
	else if (memoryType == ZG_MEMORY_TYPE_UPLOAD) pages = &mState->uploadPages;
	else sfz_assert_release(false);

	// Get index of page
	uint32_t pageIdx = findAppropriatePage(*pages, sizeBytes);

	// If no appropriate page found, allocate one
	if (pageIdx == ~0u) {
		
		// Get page size
		// Differs depending on memory type and how big the requested buffer is
		uint32_t pageSize = [&]() {
			switch (memoryType) {
			case ZG_MEMORY_TYPE_DEVICE: return PAGE_SIZE_DEVICE;
			case ZG_MEMORY_TYPE_UPLOAD: return PAGE_SIZE_UPLOAD;
			default: break;
			}
			sfz_assert_release(false);
		}();
		pageSize = std::max(pageSize, sizeBytes);

		// Allocate memory page
		MemoryPage page;
		bool createSuccess = createMemoryPage(page, pageSize, memoryType, mState->allocator);
		sfz_assert_debug(createSuccess);
		if (!createSuccess) return zg::Buffer();

		// Insert memory page into list of pages and set page index
		pageIdx = pages->size();
		pages->add(std::move(page));
	}

	// Allocate buffer
	MemoryPage& page = pages->operator[](pageIdx);
	zg::Buffer buffer;
	Block bufferBlock;
	bool bufferAllocSuccess = pageAllocateItem(page, sizeBytes, bufferBlock,
		[&](MemoryPage& page, Block allocBlock) {
		return CHECK_ZG page.heap.bufferCreate(buffer, allocBlock.offset, allocBlock.size);
	});
	sfz_assert_debug(bufferAllocSuccess);
	if (!bufferAllocSuccess) return zg::Buffer();

	// Store entry with information about allocation
	AllocEntryBuffer entry;
	entry.block = bufferBlock;
	entry.pages = pages;
	entry.heapPtr = page.heap.memoryHeap;
	mState->bufferEntries[buffer.buffer] = entry;

	// Increment total num allocation counter
	if (memoryType == ZG_MEMORY_TYPE_DEVICE) {
		mState->totalNumAllocationsDevice += 1;
	}
	else {
		mState->totalNumAllocationsUpload += 1;
	}

	return buffer;
}

zg::Texture2D DynamicGpuAllocator::allocateTexture2D(
	ZgTexture2DFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t numMipmaps,
	uint32_t* allocSizeOut) noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	sfz_assert_debug(width > 0);
	sfz_assert_debug(height > 0);
	sfz_assert_debug(numMipmaps != 0);
	sfz_assert_debug(numMipmaps <= ZG_TEXTURE_2D_MAX_NUM_MIPMAPS);

	// Fill in Texture2D create info and get allocation info in order to find suitable page
	ZgTexture2DCreateInfo createInfo = {};
	createInfo.format = format;
	createInfo.normalized = ZG_TRUE;
	createInfo.width = width;
	createInfo.height = height;
	createInfo.numMipmaps = numMipmaps;

	ZgTexture2DAllocationInfo allocInfo = {};
	CHECK_ZG zg::Texture2D::getAllocationInfo(allocInfo, createInfo);

	// Get index of page
	uint32_t pageIdx = findAppropriatePage(mState->texturePages, allocInfo.sizeInBytes);

	// If no appropriate page found, allocate one
	if (pageIdx == ~0u) {

		// Get page size
		uint32_t pageSize = std::max(PAGE_SIZE_TEXTURE, allocInfo.sizeInBytes);

		// Allocate texture page
		TexturePage page;
		bool createSuccess = createTexturePage(page, pageSize, mState->allocator);
		sfz_assert_debug(createSuccess);
		if (!createSuccess) return zg::Texture2D();

		// Insert texture page into list of pages and set page index
		pageIdx = mState->texturePages.size();
		mState->texturePages.add(std::move(page));
	}

	// Allocate texture
	TexturePage& page = mState->texturePages[pageIdx];
	zg::Texture2D texture;
	Block texBlock;
	bool texAllocSuccess = pageAllocateItem(page, allocInfo.sizeInBytes, texBlock,
		[&](TexturePage& page, Block allocBlock) {
		createInfo.offsetInBytes = allocBlock.offset;
		createInfo.sizeInBytes = allocBlock.size;
		return CHECK_ZG page.heap.texture2DCreate(texture, createInfo);
	});
	sfz_assert_debug(texAllocSuccess);
	if (!texAllocSuccess) return zg::Texture2D();

	// Store entry with information about allocation
	AllocEntryTexture entry;
	entry.block = texBlock;
	entry.heapPtr = page.heap.textureHeap;
	mState->textureEntries[texture.texture] = entry;

	// Increment total num allocation counter
	mState->totalNumAllocationsTexture += 1;

	if (allocSizeOut != nullptr)* allocSizeOut = texBlock.size;
	return texture;
}

// Deallocation methods
// ------------------------------------------------------------------------------------------------

void DynamicGpuAllocator::deallocate(zg::Buffer& buffer) noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	sfz_assert_debug(buffer.valid());

	// Get entry
	AllocEntryBuffer* entryPtr = mState->bufferEntries.get(buffer.buffer);
	sfz_assert_debug(entryPtr != nullptr);
	if (entryPtr == nullptr) return;

	// Remove entry from list of entries
	AllocEntryBuffer entry = *entryPtr;
	bool entryRemoveSuccess = mState->bufferEntries.remove(buffer.buffer);
	sfz_assert_debug(entryRemoveSuccess);

	// Release buffer
	buffer.release();

	// Reclaim space
	sfz_assert_debug(entry.pages != nullptr);
	sfz_assert_debug(entry.heapPtr != nullptr);
	bool spaceReclaimed = false;
	for (uint32_t i = 0; i < entry.pages->size(); i++) {
		MemoryPage& page = entry.pages->operator[](i);
		if (page.heap.memoryHeap == entry.heapPtr) {
			
			// Deallocate block
			pageDeallocateBlock(page, entry.block);
			
			// If page is empty, release it
			// TODO: Might potentially not want to release empty pages
			if (page.freeBlocks.size() == 1) {
				Block freeBlock = page.freeBlocks[0];
				if (freeBlock.offset == 0 && freeBlock.size == page.pageSize) {
					entry.pages->remove(i);
				}
			}

			spaceReclaimed = true;
			break;
		}
	}
	sfz_assert_release(spaceReclaimed);

	// Increment total num deallocation counter
	if (entry.pages == &mState->devicePages) {
		mState->totalNumDeallocationsDevice += 1;
	}
	else {
		mState->totalNumDeallocationsUpload += 1;
	}
}

void DynamicGpuAllocator::deallocate(zg::Texture2D& texture) noexcept
{
	std::lock_guard<std::mutex> lock(mState->mutex);
	sfz_assert_debug(texture.valid());

	// Get entry
	AllocEntryTexture* entryPtr = mState->textureEntries.get(texture.texture);
	sfz_assert_debug(entryPtr != nullptr);
	if (entryPtr == nullptr) return;

	// Remove entry from list of entries
	AllocEntryTexture entry = *entryPtr;
	bool entryRemoveSuccess = mState->textureEntries.remove(texture.texture);
	sfz_assert_debug(entryRemoveSuccess);

	// Release texture
	texture.release();

	// Reclaim space
	sfz_assert_debug(entry.heapPtr != nullptr);
	bool spaceReclaimed = false;
	for (uint32_t i = 0; i < mState->texturePages.size(); i++) {
		TexturePage& page = mState->texturePages[i];
		if (page.heap.textureHeap == entry.heapPtr) {

			// Deallocate block
			pageDeallocateBlock(page, entry.block);

			// If page is empty, release it
			// TODO: Might potentially not want to release empty pages
			if (page.freeBlocks.size() == 1) {
				Block freeBlock = page.freeBlocks[0];
				if (freeBlock.offset == 0 && freeBlock.size == page.pageSize) {
					mState->texturePages.remove(i);
				}
			}

			spaceReclaimed = true;
			break;
		}
	}
	sfz_assert_release(spaceReclaimed);

	// Increment total num deallocation counter
	mState->totalNumDeallocationsTexture += 1;
}

} // namespace ph