/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*

  A simple composite data source implementation. A composit data
  source is just a strategy for combining individual data sources into
  a collective graph.


  1) A composite data source holds a sequence of data sources. The set
     of data sources can be specified during creation of the
     database. Data sources can also be added/deleted from a database
     later.

  2) The aggregation mechanism is based on simple super-positioning of
     the graphs from the datasources. If there is a conflict (i.e., 
     data source A has a true arc from foo to bar while data source B
     has a false arc from foo to bar), the data source that it earlier
     in the sequence wins.

     The implementation below doesn't really do this and needs to be
     fixed.

*/

#include "xpcom-config.h"
#include NEW_H
#include "nsCOMPtr.h"
#include "nsIComponentManager.h"
#include "nsIEnumerator.h"
#include "nsIRDFCompositeDataSource.h"
#include "nsIRDFNode.h"
#include "nsIRDFObserver.h"
#include "nsIRDFRemoteDataSource.h"
#include "nsFixedSizeAllocator.h"
#include "nsTArray.h"
#include "nsCOMArray.h"
#include "nsArrayEnumerator.h"
#include "nsXPIDLString.h"
#include "rdf.h"
#include "nsCycleCollectionParticipant.h"

#include "nsEnumeratorUtils.h"

#ifdef DEBUG
#include "prlog.h"
#include "prprf.h"
#include <stdio.h>
PRLogModuleInfo* nsRDFLog = nullptr;
#endif

static NS_DEFINE_IID(kISupportsIID,           NS_ISUPPORTS_IID);

//----------------------------------------------------------------------
//
// CompositeDataSourceImpl
//

class CompositeEnumeratorImpl;
class CompositeArcsInOutEnumeratorImpl;
class CompositeAssertionEnumeratorImpl;

class CompositeDataSourceImpl : public nsIRDFCompositeDataSource,
                                public nsIRDFObserver
{
public:
    CompositeDataSourceImpl(void);
    CompositeDataSourceImpl(char** dataSources);

    // nsISupports interface
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(CompositeDataSourceImpl,
                                             nsIRDFCompositeDataSource)

    // nsIRDFDataSource interface
    NS_DECL_NSIRDFDATASOURCE

    // nsIRDFCompositeDataSource interface
    NS_DECL_NSIRDFCOMPOSITEDATASOURCE

    // nsIRDFObserver interface
    NS_DECL_NSIRDFOBSERVER

    bool HasAssertionN(int n, nsIRDFResource* source,
                            nsIRDFResource* property,
                            nsIRDFNode* target,
                            bool tv);

protected:
    nsCOMArray<nsIRDFObserver> mObservers;
    nsCOMArray<nsIRDFDataSource> mDataSources;

	bool        mAllowNegativeAssertions;
	bool        mCoalesceDuplicateArcs;
    int32_t     mUpdateBatchNest;

    nsFixedSizeAllocator mAllocator;

	virtual ~CompositeDataSourceImpl() {}

    friend class CompositeEnumeratorImpl;
    friend class CompositeArcsInOutEnumeratorImpl;
    friend class CompositeAssertionEnumeratorImpl;
};

//----------------------------------------------------------------------
//
// CompositeEnumeratorImpl
//

class CompositeEnumeratorImpl : public nsISimpleEnumerator
{
    // nsISupports
    NS_DECL_ISUPPORTS

    // nsISimpleEnumerator interface
    NS_DECL_NSISIMPLEENUMERATOR

    // pure abstract methods to be overridden
    virtual nsresult
    GetEnumerator(nsIRDFDataSource* aDataSource, nsISimpleEnumerator** aResult) = 0;

    virtual nsresult
    HasNegation(nsIRDFDataSource* aDataSource, nsIRDFNode* aNode, bool* aResult) = 0;

    virtual void Destroy() = 0;

protected:
    CompositeEnumeratorImpl(CompositeDataSourceImpl* aCompositeDataSource,
                            bool aAllowNegativeAssertions,
                            bool aCoalesceDuplicateArcs);

    virtual ~CompositeEnumeratorImpl();
    
    CompositeDataSourceImpl* mCompositeDataSource;

    nsISimpleEnumerator* mCurrent;
    nsIRDFNode*  mResult;
    int32_t      mNext;
    nsAutoTArray<nsCOMPtr<nsIRDFNode>, 8>  mAlreadyReturned;
    bool mAllowNegativeAssertions;
    bool mCoalesceDuplicateArcs;
};


CompositeEnumeratorImpl::CompositeEnumeratorImpl(CompositeDataSourceImpl* aCompositeDataSource,
                                                 bool aAllowNegativeAssertions,
                                                 bool aCoalesceDuplicateArcs)
    : mCompositeDataSource(aCompositeDataSource),
      mCurrent(nullptr),
      mResult(nullptr),
	  mNext(0),
      mAllowNegativeAssertions(aAllowNegativeAssertions),
      mCoalesceDuplicateArcs(aCoalesceDuplicateArcs)
{
	NS_ADDREF(mCompositeDataSource);
}


CompositeEnumeratorImpl::~CompositeEnumeratorImpl(void)
{
	NS_IF_RELEASE(mCurrent);
	NS_IF_RELEASE(mResult);
	NS_RELEASE(mCompositeDataSource);
}

NS_IMPL_ADDREF(CompositeEnumeratorImpl)
NS_IMPL_RELEASE_WITH_DESTROY(CompositeEnumeratorImpl, Destroy())
NS_IMPL_QUERY_INTERFACE1(CompositeEnumeratorImpl, nsISimpleEnumerator)

NS_IMETHODIMP
CompositeEnumeratorImpl::HasMoreElements(bool* aResult)
{
    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    nsresult rv;

    // If we've already queued up a next target, then yep, there are
    // more elements.
    if (mResult) {
        *aResult = true;
        return NS_OK;
    }

    // Otherwise, we'll need to find a next target, switching cursors
    // if necessary.
    for ( ; mNext < mCompositeDataSource->mDataSources.Count(); ++mNext) {
        if (! mCurrent) {
            // We don't have a current enumerator, so create a new one on
            // the next data source.
            nsIRDFDataSource* datasource =
                mCompositeDataSource->mDataSources[mNext];

            rv = GetEnumerator(datasource, &mCurrent);
            if (NS_FAILED(rv)) return rv;
            if (rv == NS_RDF_NO_VALUE)
                continue;

            NS_ASSERTION(mCurrent != nullptr, "you're always supposed to return an enumerator from GetEnumerator, punk.");
            if (! mCurrent)
                continue;
        }

        do {
            int32_t i;

            bool hasMore;
            rv = mCurrent->HasMoreElements(&hasMore);
            if (NS_FAILED(rv)) return rv;

            // Is the current enumerator depleted?
            if (! hasMore) {
                NS_RELEASE(mCurrent);
                break;
            }

            // Even if the current enumerator has more elements, we still
            // need to check that the current element isn't masked by
            // a negation in an earlier data source.

            // "Peek" ahead and pull out the next target.
            nsCOMPtr<nsISupports> result;
            rv = mCurrent->GetNext(getter_AddRefs(result));
            if (NS_FAILED(rv)) return rv;

            rv = result->QueryInterface(NS_GET_IID(nsIRDFNode), (void**) &mResult);
            if (NS_FAILED(rv)) return rv;

            if (mAllowNegativeAssertions)
            {
                // See if any previous data source negates this
                bool hasNegation = false;
                for (i = mNext - 1; i >= 0; --i)
                {
                    nsIRDFDataSource* datasource =
                        mCompositeDataSource->mDataSources[i];

                    rv = HasNegation(datasource, mResult, &hasNegation);
                    if (NS_FAILED(rv)) return rv;

                    if (hasNegation)
                        break;
                }

                // if so, we've gotta keep looking
                if (hasNegation)
                {
                    NS_RELEASE(mResult);
                    continue;
                }
            }

            if (mCoalesceDuplicateArcs)
            {
                // Now see if we've returned it once already.
                // XXX N.B. performance here...may want to hash if things get large?
                bool alreadyReturned = false;
                for (i = mAlreadyReturned.Length() - 1; i >= 0; --i)
                {
                    if (mAlreadyReturned[i] == mResult)
                    {
                        alreadyReturned = true;
                        break;
                    }
                }
                if (alreadyReturned)
                {
                    NS_RELEASE(mResult);
                    continue;
                }
            }

            // If we get here, then we've really found one. It'll
            // remain cached in mResult until GetNext() sucks it out.
            *aResult = true;

            // Remember that we returned it, so we don't return duplicates.

            // XXX I wonder if we should make unique-checking be
            // optional. This could get to be pretty expensive (this
            // implementation turns iteration into O(n^2)).

            if (mCoalesceDuplicateArcs)
            {
                mAlreadyReturned.AppendElement(mResult);
            }

            return NS_OK;
        } while (1);
    }

    // if we get here, there aren't any elements left.
    *aResult = false;
    return NS_OK;
}


NS_IMETHODIMP
CompositeEnumeratorImpl::GetNext(nsISupports** aResult)
{
    nsresult rv;

    bool hasMore;
    rv = HasMoreElements(&hasMore);
    if (NS_FAILED(rv)) return rv;

    if (! hasMore)
        return NS_ERROR_UNEXPECTED;

    // Don't AddRef: we "transfer" ownership to the caller
    *aResult = mResult;
    mResult = nullptr;

    return NS_OK;
}

//----------------------------------------------------------------------
//
// CompositeArcsInOutEnumeratorImpl
//
//

class CompositeArcsInOutEnumeratorImpl : public CompositeEnumeratorImpl
{
public:
    enum Type { eArcsIn, eArcsOut };

    static CompositeArcsInOutEnumeratorImpl*
    Create(nsFixedSizeAllocator& aAllocator,
           CompositeDataSourceImpl* aCompositeDataSource,
           nsIRDFNode* aNode,
           Type aType,
           bool aAllowNegativeAssertions,
           bool aCoalesceDuplicateArcs) {
        void* place = aAllocator.Alloc(sizeof(CompositeArcsInOutEnumeratorImpl));
        return place
            ? ::new (place) CompositeArcsInOutEnumeratorImpl(aCompositeDataSource,
                                                             aNode, aType,
                                                             aAllowNegativeAssertions,
                                                             aCoalesceDuplicateArcs)
            : nullptr; }

    virtual ~CompositeArcsInOutEnumeratorImpl();

    virtual nsresult
    GetEnumerator(nsIRDFDataSource* aDataSource, nsISimpleEnumerator** aResult);

    virtual nsresult
    HasNegation(nsIRDFDataSource* aDataSource, nsIRDFNode* aNode, bool* aResult);

    virtual void Destroy();

protected:
    CompositeArcsInOutEnumeratorImpl(CompositeDataSourceImpl* aCompositeDataSource,
                                     nsIRDFNode* aNode,
                                     Type aType,
                                     bool aAllowNegativeAssertions,
                                     bool aCoalesceDuplicateArcs);

private:
    nsIRDFNode* mNode;
    Type        mType;

    // Hide so that only Create() and Destroy() can be used to
    // allocate and deallocate from the heap
    static void* operator new(size_t) CPP_THROW_NEW { return 0; }
    static void operator delete(void*, size_t) {}
};


CompositeArcsInOutEnumeratorImpl::CompositeArcsInOutEnumeratorImpl(
                CompositeDataSourceImpl* aCompositeDataSource,
                nsIRDFNode* aNode,
                Type aType,
                bool aAllowNegativeAssertions,
                bool aCoalesceDuplicateArcs)
    : CompositeEnumeratorImpl(aCompositeDataSource, aAllowNegativeAssertions, aCoalesceDuplicateArcs),
      mNode(aNode),
      mType(aType)
{
    NS_ADDREF(mNode);
}

CompositeArcsInOutEnumeratorImpl::~CompositeArcsInOutEnumeratorImpl()
{
    NS_RELEASE(mNode);
}


nsresult
CompositeArcsInOutEnumeratorImpl::GetEnumerator(
                 nsIRDFDataSource* aDataSource,
                 nsISimpleEnumerator** aResult)
{
    if (mType == eArcsIn) {
        return aDataSource->ArcLabelsIn(mNode, aResult);
    }
    else {
        nsCOMPtr<nsIRDFResource> resource( do_QueryInterface(mNode) );
        return aDataSource->ArcLabelsOut(resource, aResult);
    }
}

nsresult
CompositeArcsInOutEnumeratorImpl::HasNegation(
                 nsIRDFDataSource* aDataSource,
                 nsIRDFNode* aNode,
                 bool* aResult)
{
    *aResult = false;
    return NS_OK;
}

void
CompositeArcsInOutEnumeratorImpl::Destroy()
{
    // Keep the datasource alive for the duration of the stack
    // frame so its allocator stays valid.
    nsCOMPtr<nsIRDFCompositeDataSource> kungFuDeathGrip = mCompositeDataSource;

    nsFixedSizeAllocator& pool = mCompositeDataSource->mAllocator;
    this->~CompositeArcsInOutEnumeratorImpl();
    pool.Free(this, sizeof(*this));
}


//----------------------------------------------------------------------
//
// CompositeAssertionEnumeratorImpl
//

class CompositeAssertionEnumeratorImpl : public CompositeEnumeratorImpl
{
public:
    static CompositeAssertionEnumeratorImpl*
    Create(nsFixedSizeAllocator& aAllocator,
           CompositeDataSourceImpl* aCompositeDataSource,
           nsIRDFResource* aSource,
           nsIRDFResource* aProperty,
           nsIRDFNode* aTarget,
           bool aTruthValue,
           bool aAllowNegativeAssertions,
           bool aCoalesceDuplicateArcs) {
        void* place = aAllocator.Alloc(sizeof(CompositeAssertionEnumeratorImpl));
        return place
            ? ::new (place) CompositeAssertionEnumeratorImpl(aCompositeDataSource,
                                                             aSource, aProperty, aTarget,
                                                             aTruthValue,
                                                             aAllowNegativeAssertions,
                                                             aCoalesceDuplicateArcs)
            : nullptr; }

    virtual nsresult
    GetEnumerator(nsIRDFDataSource* aDataSource, nsISimpleEnumerator** aResult);

    virtual nsresult
    HasNegation(nsIRDFDataSource* aDataSource, nsIRDFNode* aNode, bool* aResult);

    virtual void Destroy();

protected:
    CompositeAssertionEnumeratorImpl(CompositeDataSourceImpl* aCompositeDataSource,
                                     nsIRDFResource* aSource,
                                     nsIRDFResource* aProperty,
                                     nsIRDFNode* aTarget,
                                     bool aTruthValue,
                                     bool aAllowNegativeAssertions,
                                     bool aCoalesceDuplicateArcs);

    virtual ~CompositeAssertionEnumeratorImpl();

private:
    nsIRDFResource* mSource;
    nsIRDFResource* mProperty;
    nsIRDFNode*     mTarget;
    bool            mTruthValue;

    // Hide so that only Create() and Destroy() can be used to
    // allocate and deallocate from the heap
    static void* operator new(size_t) CPP_THROW_NEW { return 0; }
    static void operator delete(void*, size_t) {}
};


CompositeAssertionEnumeratorImpl::CompositeAssertionEnumeratorImpl(
                  CompositeDataSourceImpl* aCompositeDataSource,
                  nsIRDFResource* aSource,
                  nsIRDFResource* aProperty,
                  nsIRDFNode* aTarget,
                  bool aTruthValue,
                  bool aAllowNegativeAssertions,
                  bool aCoalesceDuplicateArcs)
    : CompositeEnumeratorImpl(aCompositeDataSource, aAllowNegativeAssertions, aCoalesceDuplicateArcs),
      mSource(aSource),
      mProperty(aProperty),
      mTarget(aTarget),
      mTruthValue(aTruthValue)
{
    NS_IF_ADDREF(mSource);
    NS_ADDREF(mProperty); // always must be specified
    NS_IF_ADDREF(mTarget);
}

CompositeAssertionEnumeratorImpl::~CompositeAssertionEnumeratorImpl()
{
    NS_IF_RELEASE(mSource);
    NS_RELEASE(mProperty);
    NS_IF_RELEASE(mTarget);
}


nsresult
CompositeAssertionEnumeratorImpl::GetEnumerator(
                 nsIRDFDataSource* aDataSource,
                 nsISimpleEnumerator** aResult)
{
    if (mSource) {
        return aDataSource->GetTargets(mSource, mProperty, mTruthValue, aResult);
    }
    else {
        return aDataSource->GetSources(mProperty, mTarget, mTruthValue, aResult);
    }
}

nsresult
CompositeAssertionEnumeratorImpl::HasNegation(
                 nsIRDFDataSource* aDataSource,
                 nsIRDFNode* aNode,
                 bool* aResult)
{
    if (mSource) {
        return aDataSource->HasAssertion(mSource, mProperty, aNode, !mTruthValue, aResult);
    }
    else {
        nsCOMPtr<nsIRDFResource> source( do_QueryInterface(aNode) );
        return aDataSource->HasAssertion(source, mProperty, mTarget, !mTruthValue, aResult);
    }
}

void
CompositeAssertionEnumeratorImpl::Destroy()
{
    // Keep the datasource alive for the duration of the stack
    // frame so its allocator stays valid.
    nsCOMPtr<nsIRDFCompositeDataSource> kungFuDeathGrip = mCompositeDataSource;

    nsFixedSizeAllocator& pool = mCompositeDataSource->mAllocator;
    this->~CompositeAssertionEnumeratorImpl();
    pool.Free(this, sizeof(*this));
}

////////////////////////////////////////////////////////////////////////

nsresult
NS_NewRDFCompositeDataSource(nsIRDFCompositeDataSource** result)
{
    CompositeDataSourceImpl* db = new CompositeDataSourceImpl();
    if (! db)
        return NS_ERROR_OUT_OF_MEMORY;

    *result = db;
    NS_ADDREF(*result);
    return NS_OK;
}


CompositeDataSourceImpl::CompositeDataSourceImpl(void)
	: mAllowNegativeAssertions(true),
	  mCoalesceDuplicateArcs(true),
      mUpdateBatchNest(0)
{
    static const size_t kBucketSizes[] = {
        sizeof(CompositeAssertionEnumeratorImpl),
        sizeof(CompositeArcsInOutEnumeratorImpl) };

    static const int32_t kNumBuckets = sizeof(kBucketSizes) / sizeof(size_t);

    // Per news://news.mozilla.org/39BEC105.5090206%40netscape.com
    static const int32_t kInitialSize = 256;

    mAllocator.Init("nsCompositeDataSource", kBucketSizes, kNumBuckets, kInitialSize);

#ifdef PR_LOGGING
    if (nsRDFLog == nullptr) 
        nsRDFLog = PR_NewLogModule("RDF");
#endif
}

//----------------------------------------------------------------------
//
// nsISupports interface
//

NS_IMPL_CYCLE_COLLECTION_CLASS(CompositeDataSourceImpl)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(CompositeDataSourceImpl)
    uint32_t i, count = tmp->mDataSources.Count();
    for (i = count; i > 0; --i) {
        tmp->mDataSources[i - 1]->RemoveObserver(tmp);
        tmp->mDataSources.RemoveObjectAt(i - 1);
    }
    NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMARRAY(mObservers);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(CompositeDataSourceImpl)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMARRAY(mObservers)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMARRAY(mDataSources)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END


NS_IMPL_CYCLE_COLLECTING_ADDREF(CompositeDataSourceImpl)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CompositeDataSourceImpl)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CompositeDataSourceImpl)
    NS_INTERFACE_MAP_ENTRY(nsIRDFCompositeDataSource)
    NS_INTERFACE_MAP_ENTRY(nsIRDFDataSource)
    NS_INTERFACE_MAP_ENTRY(nsIRDFObserver)
    NS_INTERFACE_MAP_ENTRY(nsIRDFCompositeDataSource)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIRDFCompositeDataSource)
NS_INTERFACE_MAP_END


//----------------------------------------------------------------------
//
// nsIRDFDataSource interface
//

NS_IMETHODIMP
CompositeDataSourceImpl::GetURI(char* *uri)
{
    *uri = nullptr;
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::GetSource(nsIRDFResource* property,
                                   nsIRDFNode* target,
                                   bool tv,
                                   nsIRDFResource** source)
{
	if (!mAllowNegativeAssertions && !tv)
		return(NS_RDF_NO_VALUE);

    int32_t count = mDataSources.Count();
    for (int32_t i = 0; i < count; ++i) {
        nsresult rv;
        rv = mDataSources[i]->GetSource(property, target, tv, source);
        if (NS_FAILED(rv)) return rv;

        if (rv == NS_RDF_NO_VALUE)
            continue;

        if (!mAllowNegativeAssertions) return(NS_OK);

        // okay, found it. make sure we don't have the opposite
        // asserted in a more local data source
        if (!HasAssertionN(count-1, *source, property, target, !tv)) 
            return NS_OK;

        NS_RELEASE(*source);
        return NS_RDF_NO_VALUE;
    }
    return NS_RDF_NO_VALUE;
}

NS_IMETHODIMP
CompositeDataSourceImpl::GetSources(nsIRDFResource* aProperty,
                                    nsIRDFNode* aTarget,
                                    bool aTruthValue,
                                    nsISimpleEnumerator** aResult)
{
    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aTarget != nullptr, "null ptr");
    if (! aTarget)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    if (! mAllowNegativeAssertions && ! aTruthValue)
        return(NS_RDF_NO_VALUE);

    *aResult = CompositeAssertionEnumeratorImpl::Create(mAllocator,
                                                        this, nullptr, aProperty,
                                                        aTarget, aTruthValue,
                                                        mAllowNegativeAssertions,
                                                        mCoalesceDuplicateArcs);

    if (! *aResult)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*aResult);
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::GetTarget(nsIRDFResource* aSource,
                                   nsIRDFResource* aProperty,
                                   bool aTruthValue,
                                   nsIRDFNode** aResult)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    if (! mAllowNegativeAssertions && ! aTruthValue)
        return(NS_RDF_NO_VALUE);

    int32_t count = mDataSources.Count();
    for (int32_t i = 0; i < count; ++i) {
        nsresult rv;
        rv = mDataSources[i]->GetTarget(aSource, aProperty, aTruthValue,
                                        aResult);
        if (NS_FAILED(rv))
            return rv;

        if (rv == NS_OK) {
            // okay, found it. make sure we don't have the opposite
            // asserted in an earlier data source

            if (mAllowNegativeAssertions) {
                if (HasAssertionN(count-1, aSource, aProperty, *aResult, !aTruthValue)) {
                    // whoops, it's been negated.
                    NS_RELEASE(*aResult);
                    return NS_RDF_NO_VALUE;
                }
            }
            return NS_OK;
        }
    }

    // Otherwise, we couldn't find it at all.
    return NS_RDF_NO_VALUE;
}

bool
CompositeDataSourceImpl::HasAssertionN(int n,
                                       nsIRDFResource* aSource,
                                       nsIRDFResource* aProperty,
                                       nsIRDFNode* aTarget,
                                       bool aTruthValue)
{
    nsresult rv;
    for (int32_t m = 0; m < n; ++m) {
        bool result;
        rv = mDataSources[m]->HasAssertion(aSource, aProperty, aTarget,
                                           aTruthValue, &result);
        if (NS_FAILED(rv))
            return false;

        // found it!
        if (result)
            return true;
    }
    return false;
}
    


NS_IMETHODIMP
CompositeDataSourceImpl::GetTargets(nsIRDFResource* aSource,
                                    nsIRDFResource* aProperty,
                                    bool aTruthValue,
                                    nsISimpleEnumerator** aResult)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    if (! mAllowNegativeAssertions && ! aTruthValue)
        return(NS_RDF_NO_VALUE);

    *aResult =
        CompositeAssertionEnumeratorImpl::Create(mAllocator, this,
                                                 aSource, aProperty, nullptr,
                                                 aTruthValue,
                                                 mAllowNegativeAssertions,
                                                 mCoalesceDuplicateArcs);

    if (! *aResult)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*aResult);
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::Assert(nsIRDFResource* aSource, 
                                nsIRDFResource* aProperty, 
                                nsIRDFNode* aTarget,
                                bool aTruthValue)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aTarget != nullptr, "null ptr");
    if (! aTarget)
        return NS_ERROR_NULL_POINTER;

    if (! mAllowNegativeAssertions && ! aTruthValue)
        return(NS_RDF_ASSERTION_REJECTED);

    nsresult rv;

    // XXX Need to add back the stuff for unblocking ...

    // We iterate backwards from the last data source which was added
    // ("the most remote") to the first ("the most local"), trying to
    // apply the assertion in each.
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        rv = mDataSources[i]->Assert(aSource, aProperty, aTarget, aTruthValue);
        if (NS_RDF_ASSERTION_ACCEPTED == rv)
            return rv;

        if (NS_FAILED(rv))
            return rv;
    }

    // nobody wanted to accept it
    return NS_RDF_ASSERTION_REJECTED;
}

NS_IMETHODIMP
CompositeDataSourceImpl::Unassert(nsIRDFResource* aSource,
                                  nsIRDFResource* aProperty,
                                  nsIRDFNode* aTarget)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aTarget != nullptr, "null ptr");
    if (! aTarget)
        return NS_ERROR_NULL_POINTER;

    nsresult rv;

    // Iterate through each of the datasources, starting with "the
    // most local" and moving to "the most remote". If _any_ of the
    // datasources have the assertion, attempt to unassert it.
    bool unasserted = true;
    int32_t i;
    int32_t count = mDataSources.Count();
    for (i = 0; i < count; ++i) {
        nsIRDFDataSource* ds = mDataSources[i];

        bool hasAssertion;
        rv = ds->HasAssertion(aSource, aProperty, aTarget, true, &hasAssertion);
        if (NS_FAILED(rv)) return rv;

        if (hasAssertion) {
            rv = ds->Unassert(aSource, aProperty, aTarget);
            if (NS_FAILED(rv)) return rv;

            if (rv != NS_RDF_ASSERTION_ACCEPTED) {
                unasserted = false;
                break;
            }
        }
    }

    // Either none of the datasources had it, or they were all willing
    // to let it be unasserted.
    if (unasserted)
        return NS_RDF_ASSERTION_ACCEPTED;

    // If we get here, one of the datasources already had the
    // assertion, and was adamant about not letting us remove
    // it. Iterate from the "most local" to the "most remote"
    // attempting to assert the negation...
    for (i = 0; i < count; ++i) {
        rv = mDataSources[i]->Assert(aSource, aProperty, aTarget, false);
        if (NS_FAILED(rv)) return rv;

        // Did it take?
        if (rv == NS_RDF_ASSERTION_ACCEPTED)
            return rv;
    }

    // Couln't get anyone to accept the negation, either.
    return NS_RDF_ASSERTION_REJECTED;
}

NS_IMETHODIMP
CompositeDataSourceImpl::Change(nsIRDFResource* aSource,
                                nsIRDFResource* aProperty,
                                nsIRDFNode* aOldTarget,
                                nsIRDFNode* aNewTarget)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aOldTarget != nullptr, "null ptr");
    if (! aOldTarget)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aNewTarget != nullptr, "null ptr");
    if (! aNewTarget)
        return NS_ERROR_NULL_POINTER;

    nsresult rv;

    // XXX So we're assuming that a datasource _must_ accept the
    // atomic change; i.e., we can't split it up across two
    // datasources. That sucks.

    // We iterate backwards from the last data source which was added
    // ("the most remote") to the first ("the most local"), trying to
    // apply the change in each.
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        rv = mDataSources[i]->Change(aSource, aProperty, aOldTarget, aNewTarget);
        if (NS_RDF_ASSERTION_ACCEPTED == rv)
            return rv;

        if (NS_FAILED(rv))
            return rv;
    }

    // nobody wanted to accept it
    return NS_RDF_ASSERTION_REJECTED;
}

NS_IMETHODIMP
CompositeDataSourceImpl::Move(nsIRDFResource* aOldSource,
                              nsIRDFResource* aNewSource,
                              nsIRDFResource* aProperty,
                              nsIRDFNode* aTarget)
{
    NS_PRECONDITION(aOldSource != nullptr, "null ptr");
    if (! aOldSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aNewSource != nullptr, "null ptr");
    if (! aNewSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aTarget != nullptr, "null ptr");
    if (! aTarget)
        return NS_ERROR_NULL_POINTER;

    nsresult rv;

    // XXX So we're assuming that a datasource _must_ accept the
    // atomic move; i.e., we can't split it up across two
    // datasources. That sucks.

    // We iterate backwards from the last data source which was added
    // ("the most remote") to the first ("the most local"), trying to
    // apply the assertion in each.
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        rv = mDataSources[i]->Move(aOldSource, aNewSource, aProperty, aTarget);
        if (NS_RDF_ASSERTION_ACCEPTED == rv)
            return rv;

        if (NS_FAILED(rv))
            return rv;
    }

    // nobody wanted to accept it
    return NS_RDF_ASSERTION_REJECTED;
}


NS_IMETHODIMP
CompositeDataSourceImpl::HasAssertion(nsIRDFResource* aSource,
                                      nsIRDFResource* aProperty,
                                      nsIRDFNode* aTarget,
                                      bool aTruthValue,
                                      bool* aResult)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aProperty != nullptr, "null ptr");
    if (! aProperty)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    if (! mAllowNegativeAssertions && ! aTruthValue)
    {
        *aResult = false;
        return(NS_OK);
    }

    nsresult rv;

    // Otherwise, look through all the data sources to see if anyone
    // has the positive...
    int32_t count = mDataSources.Count();
    for (int32_t i = 0; i < count; ++i) {
        nsIRDFDataSource* datasource = mDataSources[i];
        rv = datasource->HasAssertion(aSource, aProperty, aTarget, aTruthValue, aResult);
        if (NS_FAILED(rv)) return rv;

        if (*aResult)
            return NS_OK;

        if (mAllowNegativeAssertions)
        {
            bool hasNegation;
            rv = datasource->HasAssertion(aSource, aProperty, aTarget, !aTruthValue, &hasNegation);
            if (NS_FAILED(rv)) return rv;

            if (hasNegation)
            {
                *aResult = false;
                return NS_OK;
            }
        }
    }

    // If we get here, nobody had the assertion at all
    *aResult = false;
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::AddObserver(nsIRDFObserver* aObserver)
{
    NS_PRECONDITION(aObserver != nullptr, "null ptr");
    if (! aObserver)
        return NS_ERROR_NULL_POINTER;

    // XXX ensure uniqueness?
    mObservers.AppendObject(aObserver);

    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::RemoveObserver(nsIRDFObserver* aObserver)
{
    NS_PRECONDITION(aObserver != nullptr, "null ptr");
    if (! aObserver)
        return NS_ERROR_NULL_POINTER;

    mObservers.RemoveObject(aObserver);

    return NS_OK;
}

NS_IMETHODIMP 
CompositeDataSourceImpl::HasArcIn(nsIRDFNode *aNode, nsIRDFResource *aArc, bool *result)
{
    nsresult rv;
    *result = false;
    int32_t count = mDataSources.Count();
    for (int32_t i = 0; i < count; ++i) {
        rv = mDataSources[i]->HasArcIn(aNode, aArc, result);
        if (NS_FAILED(rv)) return rv;
        if (*result)
            return NS_OK;
    }
    return NS_OK;
}

NS_IMETHODIMP 
CompositeDataSourceImpl::HasArcOut(nsIRDFResource *aSource, nsIRDFResource *aArc, bool *result)
{
    nsresult rv;
    *result = false;
    int32_t count = mDataSources.Count();
    for (int32_t i = 0; i < count; ++i) {
        rv = mDataSources[i]->HasArcOut(aSource, aArc, result);
        if (NS_FAILED(rv)) return rv;
        if (*result)
            return NS_OK;
    }
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::ArcLabelsIn(nsIRDFNode* aTarget, nsISimpleEnumerator** aResult)
{
    NS_PRECONDITION(aTarget != nullptr, "null ptr");
    if (! aTarget)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    nsISimpleEnumerator* result = 
        CompositeArcsInOutEnumeratorImpl::Create(mAllocator, this, aTarget,
                                                 CompositeArcsInOutEnumeratorImpl::eArcsIn,
                                                 mAllowNegativeAssertions,
                                                 mCoalesceDuplicateArcs);

    if (! result)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(result);
    *aResult = result;
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::ArcLabelsOut(nsIRDFResource* aSource,
                                      nsISimpleEnumerator** aResult)
{
    NS_PRECONDITION(aSource != nullptr, "null ptr");
    if (! aSource)
        return NS_ERROR_NULL_POINTER;

    NS_PRECONDITION(aResult != nullptr, "null ptr");
    if (! aResult)
        return NS_ERROR_NULL_POINTER;

    nsISimpleEnumerator* result =
        CompositeArcsInOutEnumeratorImpl::Create(mAllocator, this, aSource,
                                                 CompositeArcsInOutEnumeratorImpl::eArcsOut,
                                                 mAllowNegativeAssertions,
                                                 mCoalesceDuplicateArcs);

    if (! result)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(result);
    *aResult = result;
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::GetAllResources(nsISimpleEnumerator** aResult)
{
    NS_NOTYETIMPLEMENTED("CompositeDataSourceImpl::GetAllResources");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CompositeDataSourceImpl::GetAllCmds(nsIRDFResource* source,
                                    nsISimpleEnumerator/*<nsIRDFResource>*/** result)
{
    nsCOMPtr<nsISupportsArray> cmdArray;
    nsresult rv;

    rv = NS_NewISupportsArray(getter_AddRefs(cmdArray));
    if (NS_FAILED(rv)) return(rv);

    for (int32_t i = 0; i < mDataSources.Count(); i++)
    {
        nsCOMPtr<nsISimpleEnumerator> dsCmds;

        rv = mDataSources[i]->GetAllCmds(source, getter_AddRefs(dsCmds));
        if (NS_SUCCEEDED(rv))
        {
            bool	hasMore = false;
            while(NS_SUCCEEDED(rv = dsCmds->HasMoreElements(&hasMore)) &&
                  hasMore)
            {
                nsCOMPtr<nsISupports>	item;
                if (NS_SUCCEEDED(rv = dsCmds->GetNext(getter_AddRefs(item))))
                {
                    // rjc: do NOT strip out duplicate commands here
                    // (due to items such as separators, it is done at a higher level)
                    cmdArray->AppendElement(item);
                }
            }
            if (NS_FAILED(rv)) return(rv);
        }
    }

    return NS_NewArrayEnumerator(result, cmdArray);
}

NS_IMETHODIMP
CompositeDataSourceImpl::IsCommandEnabled(nsISupportsArray/*<nsIRDFResource>*/* aSources,
                                          nsIRDFResource*   aCommand,
                                          nsISupportsArray/*<nsIRDFResource>*/* aArguments,
                                          bool* aResult)
{
    nsresult rv;
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        bool enabled = true;
        rv = mDataSources[i]->IsCommandEnabled(aSources, aCommand, aArguments, &enabled);
        if (NS_FAILED(rv) && (rv != NS_ERROR_NOT_IMPLEMENTED))
        {
            return(rv);
        }

        if (! enabled) {
            *aResult = false;
            return(NS_OK);
        }
    }
    *aResult = true;
    return(NS_OK);
}

NS_IMETHODIMP
CompositeDataSourceImpl::DoCommand(nsISupportsArray/*<nsIRDFResource>*/* aSources,
                                   nsIRDFResource*   aCommand,
                                   nsISupportsArray/*<nsIRDFResource>*/* aArguments)
{
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        nsresult rv = mDataSources[i]->DoCommand(aSources, aCommand, aArguments);
        if (NS_FAILED(rv) && (rv != NS_ERROR_NOT_IMPLEMENTED))
        {
            return(rv);   // all datasources must succeed
        }
    }
    return(NS_OK);
}

NS_IMETHODIMP
CompositeDataSourceImpl::BeginUpdateBatch()
{
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        mDataSources[i]->BeginUpdateBatch();
    }
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::EndUpdateBatch()
{
    for (int32_t i = mDataSources.Count() - 1; i >= 0; --i) {
        mDataSources[i]->EndUpdateBatch();
    }
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////
// nsIRDFCompositeDataSource methods
// XXX rvg We should make this take an additional argument specifying where
// in the sequence of data sources (of the db), the new data source should
// fit in. Right now, the new datasource gets stuck at the end.
// need to add the observers of the CompositeDataSourceImpl to the new data source.

NS_IMETHODIMP
CompositeDataSourceImpl::GetAllowNegativeAssertions(bool *aAllowNegativeAssertions)
{
	*aAllowNegativeAssertions = mAllowNegativeAssertions;
	return(NS_OK);
}

NS_IMETHODIMP
CompositeDataSourceImpl::SetAllowNegativeAssertions(bool aAllowNegativeAssertions)
{
	mAllowNegativeAssertions = aAllowNegativeAssertions;
	return(NS_OK);
}

NS_IMETHODIMP
CompositeDataSourceImpl::GetCoalesceDuplicateArcs(bool *aCoalesceDuplicateArcs)
{
	*aCoalesceDuplicateArcs = mCoalesceDuplicateArcs;
	return(NS_OK);
}

NS_IMETHODIMP
CompositeDataSourceImpl::SetCoalesceDuplicateArcs(bool aCoalesceDuplicateArcs)
{
	mCoalesceDuplicateArcs = aCoalesceDuplicateArcs;
	return(NS_OK);
}

NS_IMETHODIMP
CompositeDataSourceImpl::AddDataSource(nsIRDFDataSource* aDataSource)
{
    NS_ASSERTION(aDataSource != nullptr, "null ptr");
    if (! aDataSource)
        return NS_ERROR_NULL_POINTER;

    mDataSources.AppendObject(aDataSource);
    aDataSource->AddObserver(this);
    return NS_OK;
}



NS_IMETHODIMP
CompositeDataSourceImpl::RemoveDataSource(nsIRDFDataSource* aDataSource)
{
    NS_ASSERTION(aDataSource != nullptr, "null ptr");
    if (! aDataSource)
        return NS_ERROR_NULL_POINTER;


    if (mDataSources.IndexOf(aDataSource) >= 0) {
        aDataSource->RemoveObserver(this);
        mDataSources.RemoveObject(aDataSource);
    }
    return NS_OK;
}


NS_IMETHODIMP
CompositeDataSourceImpl::GetDataSources(nsISimpleEnumerator** _result)
{
    // NS_NewArrayEnumerator for an nsCOMArray takes a snapshot of the
    // current state.
    return NS_NewArrayEnumerator(_result, mDataSources);
}

NS_IMETHODIMP
CompositeDataSourceImpl::OnAssert(nsIRDFDataSource* aDataSource,
                                  nsIRDFResource* aSource,
                                  nsIRDFResource* aProperty,
                                  nsIRDFNode* aTarget)
{
    // Make sure that the assertion isn't masked by another
    // datasource.
    //
    // XXX We could make this more efficient if we knew _which_
    // datasource actually served up the OnAssert(): we could use
    // HasAssertionN() to only search datasources _before_ the
    // datasource that coughed up the assertion.
	nsresult	rv = NS_OK;

	if (mAllowNegativeAssertions)
	{   
		bool hasAssertion;
		rv = HasAssertion(aSource, aProperty, aTarget, true, &hasAssertion);
		if (NS_FAILED(rv)) return rv;

		if (! hasAssertion)
			return(NS_OK);
	}

    for (int32_t i = mObservers.Count() - 1; i >= 0; --i) {
        mObservers[i]->OnAssert(this, aSource, aProperty, aTarget);
    }
    return NS_OK;
}

NS_IMETHODIMP
CompositeDataSourceImpl::OnUnassert(nsIRDFDataSource* aDataSource,
                                    nsIRDFResource* aSource,
                                    nsIRDFResource* aProperty,
                                    nsIRDFNode* aTarget)
{
    // Make sure that the un-assertion doesn't just unmask the
    // same assertion in a different datasource.
    //
    // XXX We could make this more efficient if we knew _which_
    // datasource actually served up the OnAssert(): we could use
    // HasAssertionN() to only search datasources _before_ the
    // datasource that coughed up the assertion.
    nsresult rv;

	if (mAllowNegativeAssertions)
	{   
		bool hasAssertion;
		rv = HasAssertion(aSource, aProperty, aTarget, true, &hasAssertion);
		if (NS_FAILED(rv)) return rv;

		if (hasAssertion)
			return NS_OK;
	}

    for (int32_t i = mObservers.Count() - 1; i >= 0; --i) {
        mObservers[i]->OnUnassert(this, aSource, aProperty, aTarget);
    }
    return NS_OK;
}


NS_IMETHODIMP
CompositeDataSourceImpl::OnChange(nsIRDFDataSource* aDataSource,
                                  nsIRDFResource* aSource,
                                  nsIRDFResource* aProperty,
                                  nsIRDFNode* aOldTarget,
                                  nsIRDFNode* aNewTarget)
{
    // Make sure that the change is actually visible, and not hidden
    // by an assertion in a different datasource.
    //
    // XXX Because of aggregation, this could actually mutate into a
    // variety of OnAssert or OnChange notifications, which we'll
    // ignore for now :-/.
    for (int32_t i = mObservers.Count() - 1; i >= 0; --i) {
        mObservers[i]->OnChange(this, aSource, aProperty,
                                aOldTarget, aNewTarget);
    }
    return NS_OK;
}


NS_IMETHODIMP
CompositeDataSourceImpl::OnMove(nsIRDFDataSource* aDataSource,
                                nsIRDFResource* aOldSource,
                                nsIRDFResource* aNewSource,
                                nsIRDFResource* aProperty,
                                nsIRDFNode* aTarget)
{
    // Make sure that the move is actually visible, and not hidden
    // by an assertion in a different datasource.
    //
    // XXX Because of aggregation, this could actually mutate into a
    // variety of OnAssert or OnMove notifications, which we'll
    // ignore for now :-/.
    for (int32_t i = mObservers.Count() - 1; i >= 0; --i) {
        mObservers[i]->OnMove(this, aOldSource, aNewSource,
                              aProperty, aTarget);
    }
    return NS_OK;
}


NS_IMETHODIMP
CompositeDataSourceImpl::OnBeginUpdateBatch(nsIRDFDataSource* aDataSource)
{
    if (mUpdateBatchNest++ == 0) {
        for (int32_t i = mObservers.Count() - 1; i >= 0; --i) {
            mObservers[i]->OnBeginUpdateBatch(this);
        }
    }
    return NS_OK;
}


NS_IMETHODIMP
CompositeDataSourceImpl::OnEndUpdateBatch(nsIRDFDataSource* aDataSource)
{
    NS_ASSERTION(mUpdateBatchNest > 0, "badly nested update batch");
    if (--mUpdateBatchNest == 0) {
        for (int32_t i = mObservers.Count() - 1; i >= 0; --i) {
            mObservers[i]->OnEndUpdateBatch(this);
        }
    }
    return NS_OK;
}
