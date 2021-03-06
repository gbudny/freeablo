#include "spritecache.h"

#include <assert.h>

#include <sstream>
#include <iostream>

#include <cel/celfile.h>
#include <misc/stringops.h>


namespace FARender
{
    SpriteCache::SpriteCache(uint32_t size)
        :mNextCacheIndex(1)
        ,mCurrentSize(0)
        ,mMaxSize(size)
    {}

    SpriteCache::~SpriteCache()
    {
        for(auto block : mSpriteGroupStore)
            delete[] block;
    }

    FASpriteGroup* SpriteCache::get(const std::string& path)
    {
        if(!mStrToCache.count(path))
        {
            FASpriteGroup* newCacheEntry = allocNewSpriteGroup();
            uint32_t cacheIndex = newUniqueIndex();
            newCacheEntry->spriteCacheIndex = cacheIndex;

            std::vector<std::string> components = Misc::StringUtils::split(path, '&');

            Render::getImageInfo(components[0], newCacheEntry->width, newCacheEntry->height, newCacheEntry->animLength, 0);

            for(uint32_t i = 1; i < components.size(); i++)
            {
                std::vector<std::string> pair = Misc::StringUtils::split(components[i], '=');

                if(pair[0] == "vanim")
                {
                    std::istringstream vanimss(pair[1]);

                    uint32_t vAnim;
                    vanimss >> vAnim;

                    newCacheEntry->animLength = (newCacheEntry->height / vAnim);
                    if(newCacheEntry->height % vAnim != 0)
                        newCacheEntry->animLength++;

                    newCacheEntry->height = vAnim;
                }
            }


            mStrToCache[path] = newCacheEntry;
            mCacheToStr[cacheIndex] = path;
        }

        return mStrToCache[path];
    }

    FASpriteGroup* SpriteCache::getTileset(const std::string& celPath, const std::string& minPath, bool top)
    {
        std::stringstream ss;
        ss << celPath << ":::" << minPath << ":::" << top;
        std::string key = ss.str();

        if(!mStrToTilesetCache.count(key))
        {
            FASpriteGroup* newCacheEntry = allocNewSpriteGroup();
            uint32_t cacheIndex = newUniqueIndex();
            newCacheEntry->spriteCacheIndex = cacheIndex;
            mStrToTilesetCache[key] = newCacheEntry;
            mCacheToTilesetPath[cacheIndex] = TilesetPath(celPath, minPath, top);
        }

        return mStrToTilesetCache[key];
    }

    uint32_t SpriteCache::newUniqueIndex()
    {
        return mNextCacheIndex++;
    }

    void SpriteCache::directInsert(Render::SpriteGroup* sprite, uint32_t cacheIndex)
    {
        if(mCurrentSize >= mMaxSize)
                evict();

        mUsedList.push_front(cacheIndex);
        mCache[cacheIndex] = CacheEntry(sprite, mUsedList.begin(), true);

        mCurrentSize++;
    }

    Render::SpriteGroup* SpriteCache::get(uint32_t index)
    {
        if(!mCache.count(index))
        {
            if(mCurrentSize >= mMaxSize)
                evict();

            Render::SpriteGroup* newSprite = NULL;

            if(mCacheToStr.count(index))
            {
                //TODO: replace mCacheToStr[index] with map.at(), to guarantee thread safety (once we switch to c++11)
                // until then, it is safe in practice.
                std::string cachePath = mCacheToStr.at(index);

                std::vector<std::string> components = Misc::StringUtils::split(cachePath, '&');
                std::string sourcePath = components[0];

                uint32_t vAnim = 0;
                bool hasTrans = false;
                bool resize = false;
                bool convertToSingleTexture = false;
                uint32_t tileWidth = 0;
                uint32_t tileHeight = 0;
                uint32_t newWidth = 0;
                uint32_t newHeight = 0;
                uint32_t r=0,g=0,b=0;

                for(uint32_t i = 1; i < components.size(); i++)
                {
                    std::vector<std::string> pair = Misc::StringUtils::split(components[i], '=');

                    if(pair[0] == "trans")
                    {
                        std::vector<std::string> rgbStr = Misc::StringUtils::split(pair[1], ',');

                        hasTrans = true;

                        std::istringstream rss(rgbStr[0]);
                        rss >> r;

                        std::istringstream gss(rgbStr[1]);
                        gss >> g;

                        std::istringstream bss(rgbStr[2]);
                        bss >> b;
                    }
                    else if(pair[0] == "vanim")
                    {
                        std::istringstream vanimss(pair[1]);

                        vanimss >> vAnim;
                    }
                    else if(pair[0] == "resize")
                    {
                        resize = true;

                        std::vector<std::string> newSize = Misc::StringUtils::split(pair[1], 'x');

                        std::istringstream wss(newSize[0]);
                        wss >> newWidth;

                        std::istringstream hss(newSize[1]);
                        hss >> newHeight;
                    }
                    else if(pair[0] == "tileSize")
                    {
                        std::vector<std::string> tileSize = Misc::StringUtils::split(pair[1], 'x');

                        std::istringstream wss(tileSize[0]);
                        wss >> tileWidth;

                        std::istringstream hss(tileSize[1]);
                        hss >> tileHeight;
                    }
                    else if(pair[0] == "convertToSingleTexture")
                    {
                        convertToSingleTexture = true;
                    }
                }

                if(vAnim != 0)
                    newSprite = Render::loadVanimSprite(sourcePath, vAnim, hasTrans, r, g, b);
                else if(resize)
                    newSprite = Render::loadResizedSprite(sourcePath, newWidth, newHeight, tileWidth, tileHeight, hasTrans, r, g, b);
                else if(convertToSingleTexture)
                    newSprite = Render::loadCelToSingleTexture(sourcePath);
                else
                    newSprite = Render::loadSprite(sourcePath, hasTrans, r, g, b);
            }
            else if(mCacheToTilesetPath.count(index))
            {
                TilesetPath p = mCacheToTilesetPath[index]; //TODO: same as above
                newSprite = Render::loadTilesetSprite(p.celPath, p.minPath, p.top);
            }
            else
            {
                std::cerr << "ERROR INVALID SPRITE CACHE REQUEST " << index << std::endl;
            }

            mUsedList.push_front(index);
            mCache[index] = CacheEntry(newSprite, mUsedList.begin(), false);
            mCurrentSize++;
        }
        else
        {
            moveToFront(index);
        }

        return mCache[index].sprite;
    }

    void SpriteCache::moveToFront(uint32_t index)
    {
        mUsedList.erase(mCache.at(index).it);
        mUsedList.push_front(index);
        mCache[index].it = mUsedList.begin();
    }

    void SpriteCache::setImmortal(uint32_t index, bool immortal)
    {
        get(index);
        mCache[index].immortal = immortal;
    }

    void SpriteCache::evict()
    {
        std::list<uint32_t>::reverse_iterator it;

        for(it = mUsedList.rbegin(); it != mUsedList.rend(); it++)
        {
            if(!mCache[*it].immortal)
                break;
        }

        assert(it != mUsedList.rend() && "no evictable slots found. This should never happen");

        CacheEntry toEvict = mCache[*it];

        toEvict.sprite->destroy();
        delete toEvict.sprite;

        mCache.erase(*it);
        mUsedList.erase(--(it.base()));
        mCurrentSize--;
    }

    void SpriteCache::clear()
    {
        for(std::list<uint32_t>::iterator it = mUsedList.begin(); it != mUsedList.end(); it++)
        {
            mCache[*it].sprite->destroy();
            delete mCache[*it].sprite;
        }
    }

    std::string SpriteCache::getPathForIndex(uint32_t index)
    {
        return mCacheToStr.at(index);
    }

    FASpriteGroup* SpriteCache::allocNewSpriteGroup()
    {
        if(mSpriteGroupCurrentBlockIndex == SPRITEGROUP_STORE_BLOCK_SIZE || mSpriteGroupStore.size() == 0)
        {
            mSpriteGroupStore.push_back(new FASpriteGroup[SPRITEGROUP_STORE_BLOCK_SIZE]);
            mSpriteGroupCurrentBlockIndex = 0;
        }

        FASpriteGroup* retval = &(mSpriteGroupStore[mSpriteGroupStore.size()-1][mSpriteGroupCurrentBlockIndex]);
        mSpriteGroupCurrentBlockIndex++;

        return retval;
    }
}
