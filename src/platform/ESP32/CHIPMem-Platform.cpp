/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements heap memory allocation APIs for CHIP. These functions are platform
 *      specific and might be C Standard Library heap functions re-direction in most of cases.
 *
 */

#include <lib/support/CHIPMem.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdlib.h>

#include <esp_heap_caps.h>

#if CHIP_CONFIG_MEMORY_MGMT_PLATFORM

namespace chip {
namespace Platform {

#define VERIFY_INITIALIZED() VerifyInitialized(__func__)

static std::atomic_int memoryInitialized{ 0 };

static void VerifyInitialized(const char * func)
{
    if (!memoryInitialized)
    {
        fprintf(stderr, "ABORT: chip::Platform::%s() called before chip::Platform::MemoryInit()\n", func);
        abort();
    }
}

CHIP_ERROR MemoryAllocatorInit(void * buf, size_t bufSize)
{
    if (memoryInitialized++ > 0)
    {
        fprintf(stderr, "ABORT: chip::Platform::MemoryInit() called twice.\n");
        abort();
    }

    return CHIP_NO_ERROR;
}

void MemoryAllocatorShutdown()
{
    if (--memoryInitialized < 0)
    {
        fprintf(stderr, "ABORT: chip::Platform::MemoryShutdown() called twice.\n");
        abort();
    }
}

void * MemoryAlloc(size_t size)
{
    void * ptr;
    VERIFY_INITIALIZED();
#ifdef CONFIG_CHIP_MEM_ALLOC_MODE_INTERNAL
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#elif defined(CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL)
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    ptr = malloc(size);
#endif
    return ptr;
}

void * MemoryAlloc(size_t size, bool isLongTermAlloc)
{
    void * ptr;
    VERIFY_INITIALIZED();
#ifdef CONFIG_CHIP_MEM_ALLOC_MODE_INTERNAL
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#elif defined(CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL)
    ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    ptr = malloc(size);
#endif
    return ptr;
}

void * MemoryCalloc(size_t num, size_t size)
{
    void * ptr;
    VERIFY_INITIALIZED();
#ifdef CONFIG_CHIP_MEM_ALLOC_MODE_INTERNAL
    ptr = heap_caps_calloc(num, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#elif defined(CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL)
    ptr = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    ptr = calloc(num, size);
#endif
    return ptr;
}

void * MemoryRealloc(void * p, size_t size)
{
    VERIFY_INITIALIZED();
#ifdef CONFIG_CHIP_MEM_ALLOC_MODE_INTERNAL
    p = heap_caps_realloc(p, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#elif defined(CONFIG_CHIP_MEM_ALLOC_MODE_EXTERNAL)
    p = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    p = realloc(p, size);
#endif
    return p;
}

void MemoryFree(void * p)
{
    VERIFY_INITIALIZED();
#ifdef CONFIG_MATTER_MEM_ALLOC_MODE_DEFAULT
    free(p);
#else
    heap_caps_free(p);
#endif
}

bool MemoryInternalCheckPointer(const void * p, size_t min_size)
{
    return (p != nullptr);
}

} // namespace Platform
} // namespace chip

#endif // CHIP_CONFIG_MEMORY_MGMT_PLATFORM
