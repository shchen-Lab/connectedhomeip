/*
 *    Copyright (c) 2022 Project CHIP Authors
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

#include <lfs.h>
extern "C" {
#include <lfs_port.h>
}

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <platform/bouffalolab/common/BflbConfig.h>

#ifndef BflbConfig_LFS_NAMESPACE
#define BflbConfig_LFS_NAMESPACE "/_blcfg_"
#endif

#ifndef BflbConfig_SLASH
#define BflbConfig_SLASH '-'
#endif

namespace chip {
namespace DeviceLayer {
namespace Internal {

#if CHIP_DEVICE_LAYER_TARGET_BFLB
static struct lfs_context lfs_ctx = { .partition_name = (char *) "PSM" };
static struct lfs_config lfs_cfg  = {
     .read_size      = 256,
     .prog_size      = 256,
     .block_size     = 4096,
     .block_cycles   = 500,
     .cache_size     = 512,
     .lookahead_size = 256,
};
static lfs_t * BflbConfig_lfs = NULL;
#else
static lfs_t * BflbConfig_lfs = nullptr;
#endif

#if CHIP_DEVICE_CONFIG_BFLB_KVS_NEGATIVE_CACHE
namespace {

constexpr size_t kNegativeCacheCapacity = 64;
constexpr size_t kMaxCachedKeyLength    = PersistentStorageDelegate::kKeyLengthMax;

class NegativeKeyCache
{
public:
    bool Contains(const char * key) const
    {
        const size_t keyLength = strnlen(key, kMaxCachedKeyLength + 1);
        if (keyLength > kMaxCachedKeyLength)
        {
            return false;
        }

        for (size_t i = 0; i < kNegativeCacheCapacity; ++i)
        {
            if (mUsed[i] && strcmp(mKeys[i], key) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void Add(const char * key)
    {
        const size_t keyLength = strnlen(key, kMaxCachedKeyLength + 1);
        if (keyLength > kMaxCachedKeyLength || Contains(key))
        {
            return;
        }

        memcpy(mKeys[mNext], key, keyLength);
        mKeys[mNext][keyLength] = '\0';
        mUsed[mNext]            = true;
        mNext                   = (mNext + 1) % kNegativeCacheCapacity;
    }

    void Remove(const char * key)
    {
        const size_t keyLength = strnlen(key, kMaxCachedKeyLength + 1);
        if (keyLength > kMaxCachedKeyLength)
        {
            return;
        }

        for (size_t i = 0; i < kNegativeCacheCapacity; ++i)
        {
            if (mUsed[i] && strcmp(mKeys[i], key) == 0)
            {
                mUsed[i] = false;
                return;
            }
        }
    }

    void Clear()
    {
        memset(mUsed, 0, sizeof(mUsed));
        mNext = 0;
    }

private:
    char mKeys[kNegativeCacheCapacity][kMaxCachedKeyLength + 1] = {};
    bool mUsed[kNegativeCacheCapacity]                          = {};
    size_t mNext                                                = 0;
};

NegativeKeyCache gNegativeKeyCache;

} // namespace
#endif

#if CHIP_DEVICE_CONFIG_BFLB_KVS_FILE_HANDLE_CACHE
namespace {

static_assert(CHIP_DEVICE_CONFIG_BFLB_KVS_FILE_HANDLE_CACHE_CAPACITY > 0, "KVS file handle cache capacity must be positive");

constexpr size_t kMaxFileCachePathLength = sizeof(BflbConfig_LFS_NAMESPACE) + PersistentStorageDelegate::kKeyLengthMax;

struct CachedKvsFile
{
    lfs_file_t file                        = {};
    char path[kMaxFileCachePathLength + 1] = {};
    uint64_t lastUsed                      = 0;
    bool used                              = false;
};

class KvsFileHandleCache
{
public:
    int Open(const char * path, lfs_file_t *& file)
    {
        const size_t pathLength = strnlen(path, kMaxFileCachePathLength + 1);
        if (pathLength > kMaxFileCachePathLength)
        {
            return LFS_ERR_INVAL;
        }

        for (auto & entry : mEntries)
        {
            if (entry.used && strcmp(entry.path, path) == 0)
            {
                entry.lastUsed = ++mAccessSequence;
                file           = &entry.file;
                return LFS_ERR_OK;
            }
        }

        CachedKvsFile * target = nullptr;
        for (auto & entry : mEntries)
        {
            if (!entry.used)
            {
                target = &entry;
                break;
            }
        }

        if (target == nullptr)
        {
            struct lfs_info info;
            const int statRet = lfs_stat(BflbConfig_lfs, path, &info);
            if (statRet != LFS_ERR_OK)
            {
                return statRet;
            }

            target = &mEntries[0];
            for (auto & entry : mEntries)
            {
                if (entry.lastUsed < target->lastUsed)
                {
                    target = &entry;
                }
            }

            Close(*target);
        }

        const int ret = lfs_file_open(BflbConfig_lfs, &target->file, path, LFS_O_RDONLY);
        if (ret != LFS_ERR_OK)
        {
            return ret;
        }

        memcpy(target->path, path, pathLength);
        target->path[pathLength] = '\0';
        target->lastUsed         = ++mAccessSequence;
        target->used             = true;
        file                     = &target->file;
        return LFS_ERR_OK;
    }

    void Invalidate(const char * path)
    {
        for (auto & entry : mEntries)
        {
            if (entry.used && strcmp(entry.path, path) == 0)
            {
                Close(entry);
                return;
            }
        }
    }

    void Clear()
    {
        for (auto & entry : mEntries)
        {
            Close(entry);
        }
        mAccessSequence = 0;
    }

private:
    static void Close(CachedKvsFile & entry)
    {
        if (entry.used)
        {
            lfs_file_close(BflbConfig_lfs, &entry.file);
            entry.used     = false;
            entry.path[0]  = '\0';
            entry.lastUsed = 0;
        }
    }

    CachedKvsFile mEntries[CHIP_DEVICE_CONFIG_BFLB_KVS_FILE_HANDLE_CACHE_CAPACITY] = {};
    uint64_t mAccessSequence                                                       = 0;
};

KvsFileHandleCache gKvsFileHandleCache;

} // namespace
#endif

static void blcfg_file_cache_invalidate(const char * path)
{
#if CHIP_DEVICE_CONFIG_BFLB_KVS_FILE_HANDLE_CACHE
    gKvsFileHandleCache.Invalidate(path);
#else
    (void) path;
#endif
}

static void blcfg_file_cache_clear()
{
#if CHIP_DEVICE_CONFIG_BFLB_KVS_FILE_HANDLE_CACHE
    gKvsFileHandleCache.Clear();
#endif
}

static bool blcfg_negative_cache_contains(const char * key)
{
#if CHIP_DEVICE_CONFIG_BFLB_KVS_NEGATIVE_CACHE
    return gNegativeKeyCache.Contains(key);
#else
    (void) key;
    return false;
#endif
}

static void blcfg_negative_cache_add(const char * key)
{
#if CHIP_DEVICE_CONFIG_BFLB_KVS_NEGATIVE_CACHE
    gNegativeKeyCache.Add(key);
#else
    (void) key;
#endif
}

static void blcfg_negative_cache_remove(const char * key)
{
#if CHIP_DEVICE_CONFIG_BFLB_KVS_NEGATIVE_CACHE
    gNegativeKeyCache.Remove(key);
#else
    (void) key;
#endif
}

static void blcfg_negative_cache_clear()
{
#if CHIP_DEVICE_CONFIG_BFLB_KVS_NEGATIVE_CACHE
    gNegativeKeyCache.Clear();
#endif
}
static inline char * blcfg_convert_key(const char * pKey, const char * pNameSpace = NULL)
{
    int len_key = 0, len_namespace = 0;

    if (pNameSpace)
    {
        len_namespace = strlen(pNameSpace);
    }
    len_key = strlen(pKey);

    char * pName = (char *) malloc(len_namespace + 1 + len_key + 1);
    if (nullptr == pName)
    {
        return nullptr;
    }

    if (pNameSpace)
    {
        memcpy(pName, pNameSpace, len_namespace);
    }
    pName[len_namespace] = '/';
    memcpy(pName + len_namespace + 1, pKey, len_key);
    pName[len_namespace + 1 + len_key] = '\0';

    for (int i = len_namespace + 1; i < len_namespace + 1 + len_key; i++)
    {
        if (pName[i] == '/')
        {
            pName[i] = '_';
        }
    }

    return pName;
}

static CHIP_ERROR blcfg_do_factory_reset(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    int ret;
    struct lfs_info stat;
    lfs_file_t file;
    lfs_dir_t dir            = {};
    char * factory_reset_key = blcfg_convert_key(BflbConfig::kBLKey_factoryResetFlag);

    if (factory_reset_key == nullptr)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);
    blcfg_file_cache_clear();

    ret = lfs_stat(BflbConfig_lfs, factory_reset_key, &stat);

    if (LFS_ERR_OK == ret)
    {
        err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;

        do
        {
            ret = lfs_file_open(BflbConfig_lfs, &file, factory_reset_key, LFS_O_RDONLY);
            VerifyOrExit(ret == LFS_ERR_OK, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);
            lfs_file_close(BflbConfig_lfs, &file);

            ret = lfs_dir_open(BflbConfig_lfs, &dir, BflbConfig_LFS_NAMESPACE);
            VerifyOrExit(ret == LFS_ERR_OK, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);

            while (1)
            {
                ret = lfs_dir_read(BflbConfig_lfs, &dir, &stat);
                if (ret <= 0)
                {
                    break;
                }

                if (stat.type != LFS_TYPE_REG)
                {
                    continue;
                }
                char * delete_key = blcfg_convert_key(stat.name, BflbConfig_LFS_NAMESPACE);
                VerifyOrExit(delete_key != nullptr, err = CHIP_ERROR_NO_MEMORY);

                ret = lfs_remove(BflbConfig_lfs, delete_key);
                free(delete_key);

                if (ret != LFS_ERR_OK)
                {
                    break;
                }
            }

            lfs_dir_close(BflbConfig_lfs, &dir);

            ret = lfs_remove(BflbConfig_lfs, factory_reset_key);
            if (ret != LFS_ERR_OK)
            {
                break;
            }

            err = CHIP_NO_ERROR;
        } while (0);
    }

exit:
    blcfg_negative_cache_clear();
    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
    if (factory_reset_key)
    {
        free(factory_reset_key);
    }

    return err;
}

void BflbConfig::Init(void)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    int ret;
    struct lfs_info stat;

#if CHIP_DEVICE_LAYER_TARGET_BFLB
    BflbConfig_lfs = lfs_xip_init(&lfs_ctx, &lfs_cfg);
#else
    BflbConfig_lfs = lfs_xip_init();
#endif
    VerifyOrExit(BflbConfig_lfs != NULL, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);

    /* init namespace */
    ret = lfs_stat(BflbConfig_lfs, BflbConfig_LFS_NAMESPACE, &stat);
    if (ret != LFS_ERR_OK)
    {
        ret = lfs_mkdir(BflbConfig_lfs, BflbConfig_LFS_NAMESPACE);
        VerifyOrExit(ret == LFS_ERR_OK, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);

        ret = lfs_stat(BflbConfig_lfs, BflbConfig_LFS_NAMESPACE, &stat);
    }
    VerifyOrExit(ret == LFS_ERR_OK && stat.type == LFS_TYPE_DIR, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);

    err = blcfg_do_factory_reset();
exit:
    configASSERT(err == CHIP_NO_ERROR);
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, uint8_t * val, size_t size, size_t & readsize)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    int ret        = LFS_ERR_OK;
    lfs_file_t file;
    char * read_key = blcfg_convert_key(key, BflbConfig_LFS_NAMESPACE);

    if (read_key == nullptr)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);

    if (blcfg_negative_cache_contains(key))
    {
        err = CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
        goto exit;
    }

    ret = lfs_file_open(BflbConfig_lfs, &file, read_key, LFS_O_RDONLY);
    if (ret != LFS_ERR_OK)
    {
        if (ret == LFS_ERR_NOENT)
        {
            blcfg_negative_cache_add(key);
        }
        err = CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
        goto exit;
    }

    if (val && size)
    {
        ret      = lfs_file_read(BflbConfig_lfs, &file, val, size);
        readsize = ret;
    }
    lfs_file_close(BflbConfig_lfs, &file);

exit:
    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
    free(read_key);

    return err;
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, bool & val)
{
    size_t readlen = 0;
    return ReadConfigValue(key, (uint8_t *) &val, 1, readlen);
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, uint32_t & val)
{
    size_t readlen = 0;
    return ReadConfigValue(key, (uint8_t *) &val, sizeof(val), readlen);
}

CHIP_ERROR BflbConfig::ReadConfigValue(const char * key, uint64_t & val)
{
    size_t readlen = 0;
    return ReadConfigValue(key, (uint8_t *) &val, sizeof(val), readlen);
}

CHIP_ERROR BflbConfig::ReadConfigValueStr(const char * key, char * buf, size_t bufSize, size_t & outLen)
{
    size_t readlen = 0;
    if (CHIP_NO_ERROR == ReadConfigValue(key, (uint8_t *) buf, bufSize, readlen))
    {
        outLen = readlen;
        if (readlen && readlen < bufSize)
        {
            buf[readlen] = '\0';
        }

        return CHIP_NO_ERROR;
    }

    return CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

CHIP_ERROR BflbConfig::ReadConfigValueBin(const char * key, uint8_t * buf, size_t bufSize, size_t & outLen)
{
    size_t readlen = 0;
    if (CHIP_NO_ERROR == ReadConfigValue(key, (uint8_t *) buf, bufSize, readlen))
    {
        outLen = readlen;
        return CHIP_NO_ERROR;
    }

    return CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, uint8_t * val, size_t size)
{
    int ret        = LFS_ERR_OK;
    CHIP_ERROR err = CHIP_NO_ERROR;
    lfs_file_t file;
    char * write_key = blcfg_convert_key(key, BflbConfig_LFS_NAMESPACE);

    if (write_key == nullptr)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);
    blcfg_file_cache_invalidate(write_key);
    blcfg_negative_cache_remove(key);

    ret = lfs_file_open(BflbConfig_lfs, &file, write_key, LFS_O_CREAT | LFS_O_RDWR | LFS_O_TRUNC);
    VerifyOrExit(ret == LFS_ERR_OK, err = CHIP_ERROR_PERSISTED_STORAGE_FAILED);

    lfs_file_write(BflbConfig_lfs, &file, val, size);
    lfs_file_close(BflbConfig_lfs, &file);

exit:
    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
    free(write_key);

    return err;
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, bool val)
{
    return WriteConfigValue(key, (uint8_t *) &val, sizeof(val));
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, uint32_t val)
{
    return WriteConfigValue(key, (uint8_t *) &val, sizeof(val));
}

CHIP_ERROR BflbConfig::WriteConfigValue(const char * key, uint64_t val)
{
    return WriteConfigValue(key, (uint8_t *) &val, sizeof(val));
}

CHIP_ERROR BflbConfig::WriteConfigValueStr(const char * key, const char * str)
{
    return WriteConfigValue(key, (uint8_t *) str, strlen(str) + 1);
}

CHIP_ERROR BflbConfig::WriteConfigValueStr(const char * key, const char * str, size_t strLen)
{
    return WriteConfigValue(key, (uint8_t *) str, strLen);
}

CHIP_ERROR BflbConfig::WriteConfigValueBin(const char * key, const uint8_t * data, size_t dataLen)
{
    return WriteConfigValue(key, (uint8_t *) data, dataLen);
}

CHIP_ERROR BflbConfig::ClearConfigValue(const char * key)
{
    char * delete_key = blcfg_convert_key(key, BflbConfig_LFS_NAMESPACE);

    if (delete_key == nullptr)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);
    blcfg_file_cache_invalidate(delete_key);
    int ret = lfs_remove(BflbConfig_lfs, delete_key);
    if (ret >= LFS_ERR_OK || ret == LFS_ERR_NOENT)
    {
        blcfg_negative_cache_add(key);
    }
    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);

    free(delete_key);

    return (ret >= LFS_ERR_OK || ret == LFS_ERR_NOENT) ? CHIP_NO_ERROR : CHIP_ERROR_PERSISTED_STORAGE_FAILED;
}

CHIP_ERROR BflbConfig::FactoryResetConfig(void)
{
    int ret = LFS_ERR_OK;
    lfs_file_t file;
    char * reset_key             = blcfg_convert_key(kBLKey_factoryResetFlag);
    const char reset_key_value[] = "pending";

    if (nullptr == reset_key)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);

    ret = lfs_file_open(BflbConfig_lfs, &file, reset_key, LFS_O_CREAT | LFS_O_RDWR | LFS_O_TRUNC);
    if (ret != LFS_ERR_OK)
    {
        BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
        return CHIP_ERROR_PERSISTED_STORAGE_FAILED;
    }

    lfs_file_write(BflbConfig_lfs, &file, reset_key_value, sizeof(reset_key_value));
    lfs_file_close(BflbConfig_lfs, &file);

    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
    free(reset_key);

    return blcfg_do_factory_reset();
}

void BflbConfig::RunConfigUnitTest() {}

bool BflbConfig::ConfigValueExists(const char * key)
{
    char * exist_key = blcfg_convert_key(key, BflbConfig_LFS_NAMESPACE);
    struct lfs_info stat;

    if (exist_key == nullptr)
    {
        return false;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);
    if (blcfg_negative_cache_contains(key))
    {
        BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
        free(exist_key);
        return false;
    }

    const int ret = lfs_stat(BflbConfig_lfs, exist_key, &stat);
    if (ret == LFS_ERR_NOENT)
    {
        blcfg_negative_cache_add(key);
    }
    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);

    free(exist_key);

    return ret == LFS_ERR_OK;
}

CHIP_ERROR BflbConfig::ReadKVS(const char * key, void * value, size_t value_size, size_t * read_bytes_size, size_t offset_bytes)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    int ret        = LFS_ERR_OK;
    lfs_file_t localFile;
    lfs_file_t * readFile = &localFile;
    bool fileOpened       = false;
    bool keepFileOpen     = false;
    char * read_key       = blcfg_convert_key(key, BflbConfig_LFS_NAMESPACE);

    if (read_bytes_size)
    {
        *read_bytes_size = 0;
    }

    if (read_key == nullptr)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    BflbConfig_lfs->cfg->lock(BflbConfig_lfs->cfg);

    if (blcfg_negative_cache_contains(key))
    {
        err = CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
        goto exit;
    }

#if CHIP_DEVICE_CONFIG_BFLB_KVS_FILE_HANDLE_CACHE
    ret          = gKvsFileHandleCache.Open(read_key, readFile);
    keepFileOpen = (ret == LFS_ERR_OK);
#else
    ret = lfs_file_open(BflbConfig_lfs, readFile, read_key, LFS_O_RDONLY);
#endif
    if (ret != LFS_ERR_OK)
    {
        if (ret == LFS_ERR_NOENT)
        {
            blcfg_negative_cache_add(key);
        }
        err = CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND;
        goto exit;
    }
    fileOpened = true;

    if (value && value_size)
    {
        if (lfs_file_seek(BflbConfig_lfs, readFile, offset_bytes, LFS_SEEK_SET) < 0)
        {
            err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;
        }
        else
        {
            ret = lfs_file_read(BflbConfig_lfs, readFile, value, value_size);
            if (ret < 0)
            {
                err = CHIP_ERROR_PERSISTED_STORAGE_FAILED;
            }
            else if (read_bytes_size)
            {
                *read_bytes_size = static_cast<size_t>(ret);
            }
        }
    }

    if (fileOpened)
    {
        if (keepFileOpen)
        {
            if (err != CHIP_NO_ERROR)
            {
                blcfg_file_cache_invalidate(read_key);
            }
        }
        else
        {
            lfs_file_close(BflbConfig_lfs, readFile);
        }
    }

exit:
    BflbConfig_lfs->cfg->unlock(BflbConfig_lfs->cfg);
    free(read_key);

    return err;
}

CHIP_ERROR BflbConfig::WriteKVS(const char * key, const void * value, size_t value_size)
{
    return WriteConfigValueBin(key, (const uint8_t *) value, value_size);
}

CHIP_ERROR BflbConfig::ClearKVS(const char * key)
{
    return ClearConfigValue(key);
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
