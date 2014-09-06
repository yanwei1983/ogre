/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
  -----------------------------------------------------------------------------
*/

#include "OgreStableHeaders.h"
#include "OgreItem.h"

#include "OgreMeshManager.h"
#include "OgreMesh2.h"
#include "OgreSubMesh2.h"
#include "OgreSubItem.h"
#include "OgreHlmsManager.h"
#include "OgreException.h"
#include "OgreSceneManager.h"
#include "OgreLogManager.h"
#include "Animation/OgreSkeletonInstance.h"
#include "OgreCamera.h"
#include "OgreAxisAlignedBox.h"
#include "OgreVector4.h"
#include "OgreRoot.h"
#include "OgreTechnique.h"
#include "OgrePass.h"
#include "OgreOptimisedUtil.h"
#include "OgreSceneNode.h"
#include "OgreLodStrategy.h"
#include "OgreLodListener.h"
#include "OgreMaterialManager.h"

namespace Ogre {
    extern const FastArray<Real> c_DefaultLodMesh;
    //-----------------------------------------------------------------------
    Item::Item( IdType id, ObjectMemoryManager *objectMemoryManager )
        : MovableObject( id, objectMemoryManager, 0 ),
          mSkeletonInstance( 0 ),
          mInitialised( false )
    {
        mObjectData.mQueryFlags[mObjectData.mIndex] = SceneManager::QUERY_ENTITY_DEFAULT_MASK;
    }
    //-----------------------------------------------------------------------
    Item::Item( IdType id, ObjectMemoryManager *objectMemoryManager, const MeshPtr& mesh ) :
        MovableObject( id, objectMemoryManager, 0 ),
        mMesh( mesh ),
        mSkeletonInstance( 0 ),
        mInitialised( false )
    {
        _initialise();
        mObjectData.mQueryFlags[mObjectData.mIndex] = SceneManager::QUERY_ENTITY_DEFAULT_MASK;
    }
    //-----------------------------------------------------------------------
    void Item::_initialise(bool forceReinitialise)
    {
        if( forceReinitialise )
            _deinitialise();

        if (mInitialised)
            return;

        if (mMesh->isBackgroundLoaded() && !mMesh->isLoaded())
        {
            // register for a callback when mesh is finished loading
            // do this before asking for load to happen to avoid race
            mMesh->addListener(this);
        }
        
        // On-demand load
        mMesh->load();
        // If loading failed, or deferred loading isn't done yet, defer
        // Will get a callback in the case of deferred loading
        // Skeletons are cascade-loaded so no issues there
        if (!mMesh->isLoaded())
            return;

        // Is mesh skeletally animated?
        if( mMesh->hasSkeleton() && !mMesh->getSkeleton().isNull() && mManager )
        {
            const SkeletonDef *skeletonDef = mMesh->getSkeleton().get();
            mSkeletonInstance = mManager->createSkeletonInstance( skeletonDef );
        }

        mLodMesh = mMesh->_getLodValueArray();

        // Build main subItem list
        buildSubItems();

        {
            //Without filling the renderables list, the RenderQueue won't
            //catch our sub entities and thus we won't be rendered
            mRenderables.reserve( mSubItems.size() );
            SubItemVec::iterator itor = mSubItems.begin();
            SubItemVec::iterator end  = mSubItems.end();
            while( itor != end )
            {
                mRenderables.push_back( &(*itor) );
                ++itor;
            }
        }

        Aabb aabb( mMesh->getAabb() );
        mObjectData.mLocalAabb->setFromAabb( aabb, mObjectData.mIndex );
        mObjectData.mWorldAabb->setFromAabb( aabb, mObjectData.mIndex );
        mObjectData.mLocalRadius[mObjectData.mIndex] = aabb.getRadius();
        mObjectData.mWorldRadius[mObjectData.mIndex] = aabb.getRadius();

        mInitialised = true;
    }
    //-----------------------------------------------------------------------
    void Item::_deinitialise(void)
    {
        if (!mInitialised)
            return;

        // Delete submeshes
        mSubItems.clear();
        mRenderables.clear();

        OGRE_DELETE mSkeletonInstance;
        mSkeletonInstance = 0;

        mInitialised = false;
    }
    //-----------------------------------------------------------------------
    Item::~Item()
    {
        _deinitialise();
        // Unregister our listener
        mMesh->removeListener(this);
    }
    //-----------------------------------------------------------------------
    const MeshPtr& Item::getMesh(void) const
    {
        return mMesh;
    }
    //-----------------------------------------------------------------------
    SubItem* Item::getSubItem(size_t index)
    {
        if (index >= mSubItems.size())
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                        "Index out of bounds.",
                        "Item::getSubItem");
        return &mSubItems[index];
    }
    //-----------------------------------------------------------------------
    const SubItem* Item::getSubItem(size_t index) const
    {
        if (index >= mSubItems.size())
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
            "Index out of bounds.",
            "Item::getSubItem");
        return &mSubItems[index];
    }
    //-----------------------------------------------------------------------
    size_t Item::getNumSubItems(void) const
    {
        return mSubItems.size();
    }
    //-----------------------------------------------------------------------
    void Item::setDatablock( HlmsDatablock *datablock )
    {
        SubItemVec::iterator itor = mSubItems.begin();
        SubItemVec::iterator end  = mSubItems.end();

        while( itor != end )
        {
            itor->setDatablock( datablock );
            ++itor;
        }
    }
#if 0
    //-----------------------------------------------------------------------
    Item* Item::clone( const String& newName ) const
    {
        if (!mManager)
        {
            OGRE_EXCEPT(Exception::ERR_ITEM_NOT_FOUND, 
                        "Cannot clone an Item that wasn't created through a "
                        "SceneManager", "Item::clone");
        }
        Item* newEnt = mManager->createItem( newName, getMesh()->getName() );

        if( mInitialised )
        {
            // Copy material settings
            SubItemVec::const_iterator i;
            unsigned int n = 0;
            for (i = mSubItems.begin(); i != mSubItems.end(); ++i, ++n)
                newEnt->getSubItem(n)->setDatablock( i->getDatablock() );
        }

        return newEnt;
    }
#endif
    //-----------------------------------------------------------------------
    void Item::setMaterialName( const String& name, const String& groupName /* = ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME */)
    {
        // Set for all subentities
        SubItemVec::iterator i;
        for (i = mSubItems.begin(); i != mSubItems.end(); ++i)
        {
            i->setMaterialName(name, groupName);
        }
    }
    //-----------------------------------------------------------------------
    void Item::setMaterial( const MaterialPtr& material )
    {
        // Set for all subentities
        SubItemVec::iterator i;
        for (i = mSubItems.begin(); i != mSubItems.end(); ++i)
        {
            i->setMaterial(material);
        }
    }
    //-----------------------------------------------------------------------
    const String& Item::getMovableType(void) const
    {
        return ItemFactory::FACTORY_TYPE_NAME;
    }
    //-----------------------------------------------------------------------
    void Item::buildSubItems(void)
    {
        // Create SubEntities
        size_t numSubMeshes = mMesh->getNumSubMeshes();
        mSubItems.reserve( numSubMeshes );
        for( size_t i=0; i<numSubMeshes; ++i )
        {
            SubMesh *subMesh = mMesh->getSubMesh(i);
            mSubItems.push_back( SubItem( this, subMesh ) );

            if( !subMesh->mMaterialName.empty() )
            {
                //Try first Hlms materials, then the low level ones.
                HlmsManager *hlmsManager = Root::getSingleton().getHlmsManager();
                HlmsDatablock *datablock = hlmsManager->getDatablockNoDefault( subMesh->mMaterialName );

                if( !datablock )
                    mSubItems.back().setDatablock( datablock );
                else
                    mSubItems.back().setMaterialName( subMesh->mMaterialName, mMesh->getGroup() );
            }
        }
    }
    //-----------------------------------------------------------------------
    void Item::_notifyAttached( Node* parent )
    {
        MovableObject::_notifyAttached(parent);

        if( mSkeletonInstance )
            mSkeletonInstance->setParentNode( parent );
    }
    //-----------------------------------------------------------------------
    /*void Item::shareSkeletonInstanceWith(Item* item)
    {
        if (item->getMesh()->getSkeleton() != getMesh()->getSkeleton())
        {
            OGRE_EXCEPT(Exception::ERR_RT_ASSERTION_FAILED,
                "The supplied Item has a different skeleton.",
                "Item::shareSkeletonWith");
        }
        if (!mSkeletonInstance)
        {
            OGRE_EXCEPT(Exception::ERR_RT_ASSERTION_FAILED,
                "This Item has no skeleton.",
                "Item::shareSkeletonWith");
        }
        if (mSharedSkeletonEntities != NULL && item->mSharedSkeletonEntities != NULL)
        {
            OGRE_EXCEPT(Exception::ERR_RT_ASSERTION_FAILED,
                "Both entities already shares their SkeletonInstances! At least "
                "one of the instances must not share it's instance.",
                "Item::shareSkeletonWith");
        }

        //check if we already share our skeletoninstance, we don't want to delete it if so
        if (mSharedSkeletonEntities != NULL)
        {
            Item->shareSkeletonInstanceWith(this);
        }
        else
        {
            OGRE_DELETE mSkeletonInstance;
            mSkeletonInstance = item->mSkeletonInstance;

            if (item->mSharedSkeletonEntities == NULL)
            {
                item->mSharedSkeletonEntities = OGRE_NEW_T(ItemSet, MEMCATEGORY_ANIMATION)();
                item->mSharedSkeletonEntities->insert(Item);
            }
            mSharedSkeletonEntities = item->mSharedSkeletonEntities;
            mSharedSkeletonEntities->insert(this);
        }
    }
    //-----------------------------------------------------------------------
    void Item::stopSharingSkeletonInstance()
    {
        if (mSharedSkeletonEntities == NULL)
        {
            OGRE_EXCEPT(Exception::ERR_RT_ASSERTION_FAILED,
                "This Item is not sharing it's skeletoninstance.",
                "Item::shareSkeletonWith");
        }
        //check if there's no other than us sharing the skeleton instance
        if (mSharedSkeletonEntities->size() == 1)
        {
            //just reset
            OGRE_DELETE_T(mSharedSkeletonEntities, ItemSet, MEMCATEGORY_ANIMATION);
            mSharedSkeletonEntities = 0;
        }
        else
        {
            mSkeletonInstance = OGRE_NEW SkeletonInstance( mMesh->getSkeleton() );
            mSkeletonInstance->load();
            mSharedSkeletonEntities->erase(this);
            if (mSharedSkeletonEntities->size() == 1)
            {
                (*mSharedSkeletonEntities->begin())->stopSharingSkeletonInstance();
            }
            mSharedSkeletonEntities = 0;
        }
    }*/

    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    String ItemFactory::FACTORY_TYPE_NAME = "Item";
    //-----------------------------------------------------------------------
    const String& ItemFactory::getType(void) const
    {
        return FACTORY_TYPE_NAME;
    }
    //-----------------------------------------------------------------------
    MovableObject* ItemFactory::createInstanceImpl( IdType id,
                                            ObjectMemoryManager *objectMemoryManager,
                                            const NameValuePairList* params )
    {
        // must have mesh parameter
        MeshPtr pMesh;
        if (params != 0)
        {
            String groupName = ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME;

            NameValuePairList::const_iterator ni;

            ni = params->find("resourceGroup");
            if (ni != params->end())
            {
                groupName = ni->second;
            }

            ni = params->find("mesh");
            if (ni != params->end())
            {
#if 0
                // Get mesh (load if required)
                pMesh = MeshManager::getSingleton().load( ni->second, groupName );
#endif
            }

        }
        if (pMesh.isNull())
        {
            OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
                "'mesh' parameter required when constructing an Item.",
                "ItemFactory::createInstance");
        }

        return OGRE_NEW Item( id, objectMemoryManager, pMesh );
    }
    //-----------------------------------------------------------------------
    void ItemFactory::destroyInstance( MovableObject* obj)
    {
        OGRE_DELETE obj;
    }


}