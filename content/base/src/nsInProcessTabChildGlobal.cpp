/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8; -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsInProcessTabChildGlobal.h"
#include "nsContentUtils.h"
#include "nsIScriptSecurityManager.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsEventDispatcher.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsIJSRuntimeService.h"
#include "nsComponentManagerUtils.h"
#include "nsNetUtil.h"
#include "nsScriptLoader.h"
#include "nsIJSContextStack.h"
#include "nsFrameLoader.h"
#include "xpcpublic.h"
#include "nsIMozBrowserFrame.h"
#include "nsDOMClassInfoID.h"
#include "mozilla/dom/StructuredCloneUtils.h"

using mozilla::dom::StructuredCloneData;
using mozilla::dom::StructuredCloneClosure;

bool SendSyncMessageToParent(void* aCallbackData,
                             const nsAString& aMessage,
                             const StructuredCloneData& aData,
                             InfallibleTArray<nsString>* aJSONRetVal)
{
  nsInProcessTabChildGlobal* tabChild =
    static_cast<nsInProcessTabChildGlobal*>(aCallbackData);
  nsCOMPtr<nsIContent> owner = tabChild->mOwner;
  nsTArray<nsCOMPtr<nsIRunnable> > asyncMessages;
  asyncMessages.SwapElements(tabChild->mASyncMessages);
  uint32_t len = asyncMessages.Length();
  for (uint32_t i = 0; i < len; ++i) {
    nsCOMPtr<nsIRunnable> async = asyncMessages[i];
    async->Run();
  }
  if (tabChild->mChromeMessageManager) {
    nsRefPtr<nsFrameMessageManager> mm = tabChild->mChromeMessageManager;
    mm->ReceiveMessage(owner, aMessage, true, &aData, nullptr, aJSONRetVal);
  }
  return true;
}

class nsAsyncMessageToParent : public nsRunnable
{
public:
  nsAsyncMessageToParent(nsInProcessTabChildGlobal* aTabChild,
                         const nsAString& aMessage,
                         const StructuredCloneData& aData)
    : mTabChild(aTabChild), mMessage(aMessage)
  {
    if (aData.mDataLength && !mData.copy(aData.mData, aData.mDataLength)) {
      NS_RUNTIMEABORT("OOM");
    }
    mClosure = aData.mClosure;
  }

  NS_IMETHOD Run()
  {
    mTabChild->mASyncMessages.RemoveElement(this);
    if (mTabChild->mChromeMessageManager) {
      StructuredCloneData data;
      data.mData = mData.data();
      data.mDataLength = mData.nbytes();
      data.mClosure = mClosure;

      nsRefPtr<nsFrameMessageManager> mm = mTabChild->mChromeMessageManager;
      mm->ReceiveMessage(mTabChild->mOwner, mMessage, false, &data,
                         nullptr, nullptr, nullptr);
    }
    return NS_OK;
  }
  nsRefPtr<nsInProcessTabChildGlobal> mTabChild;
  nsString mMessage;
  JSAutoStructuredCloneBuffer mData;
  StructuredCloneClosure mClosure;
};

bool SendAsyncMessageToParent(void* aCallbackData,
                              const nsAString& aMessage,
                              const StructuredCloneData& aData)
{
  nsInProcessTabChildGlobal* tabChild =
    static_cast<nsInProcessTabChildGlobal*>(aCallbackData);
  nsCOMPtr<nsIRunnable> ev =
    new nsAsyncMessageToParent(tabChild, aMessage, aData);
  tabChild->mASyncMessages.AppendElement(ev);
  NS_DispatchToCurrentThread(ev);
  return true;
}

nsInProcessTabChildGlobal::nsInProcessTabChildGlobal(nsIDocShell* aShell,
                                                     nsIContent* aOwner,
                                                     nsFrameMessageManager* aChrome)
: mDocShell(aShell), mInitialized(false), mLoadingScript(false),
  mDelayedDisconnect(false), mOwner(aOwner), mChromeMessageManager(aChrome)
{

  // If owner corresponds to an <iframe mozbrowser>, we'll have to tweak our
  // PreHandleEvent implementation.
  nsCOMPtr<nsIMozBrowserFrame> browserFrame = do_QueryInterface(mOwner);
  bool isBrowser = false;
  if (browserFrame) {
    browserFrame->GetReallyIsBrowser(&isBrowser);
  }
  mIsBrowserFrame = isBrowser;
}

nsInProcessTabChildGlobal::~nsInProcessTabChildGlobal()
{
  NS_ASSERTION(!mCx, "Couldn't release JSContext?!?");
}

nsresult
nsInProcessTabChildGlobal::Init()
{
#ifdef DEBUG
  nsresult rv =
#endif
  InitTabChildGlobal();
  NS_WARN_IF_FALSE(NS_SUCCEEDED(rv),
                   "Couldn't initialize nsInProcessTabChildGlobal");
  mMessageManager = new nsFrameMessageManager(false,
                                              SendSyncMessageToParent,
                                              SendAsyncMessageToParent,
                                              nullptr,
                                              this,
                                              nullptr,
                                              mCx);

  // Set the location information for the new global, so that tools like
  // about:memory may use that information.
  JSObject *global;
  nsIURI* docURI = mOwner->OwnerDoc()->GetDocumentURI();
  if (mGlobal && NS_SUCCEEDED(mGlobal->GetJSObject(&global)) && docURI) {
    xpc::SetLocationForGlobal(global, docURI);
  }

  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsInProcessTabChildGlobal)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsInProcessTabChildGlobal,
                                                nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mMessageManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mGlobal)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsInProcessTabChildGlobal,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mMessageManager)
  nsFrameScriptExecutor::Traverse(tmp, cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsInProcessTabChildGlobal)
  NS_INTERFACE_MAP_ENTRY(nsIFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsISyncMessageSender)
  NS_INTERFACE_MAP_ENTRY(nsIContentFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsIInProcessContentFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsIScriptContextPrincipal)
  NS_INTERFACE_MAP_ENTRY(nsIScriptObjectPrincipal)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(ContentFrameMessageManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(nsInProcessTabChildGlobal, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsInProcessTabChildGlobal, nsDOMEventTargetHelper)

NS_IMETHODIMP
nsInProcessTabChildGlobal::GetContent(nsIDOMWindow** aContent)
{
  *aContent = nullptr;
  nsCOMPtr<nsIDOMWindow> window = do_GetInterface(mDocShell);
  window.swap(*aContent);
  return NS_OK;
}

NS_IMETHODIMP
nsInProcessTabChildGlobal::GetDocShell(nsIDocShell** aDocShell)
{
  NS_IF_ADDREF(*aDocShell = mDocShell);
  return NS_OK;
}

NS_IMETHODIMP
nsInProcessTabChildGlobal::Btoa(const nsAString& aBinaryData,
                            nsAString& aAsciiBase64String)
{
  return nsContentUtils::Btoa(aBinaryData, aAsciiBase64String);
}

NS_IMETHODIMP
nsInProcessTabChildGlobal::Atob(const nsAString& aAsciiString,
                            nsAString& aBinaryData)
{
  return nsContentUtils::Atob(aAsciiString, aBinaryData);
}


NS_IMETHODIMP
nsInProcessTabChildGlobal::PrivateNoteIntentionalCrash()
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

void
nsInProcessTabChildGlobal::Disconnect()
{
  // Let the frame scripts know the child is being closed. We do any other
  // cleanup after the event has been fired. See DelayedDisconnect
  nsContentUtils::AddScriptRunner(
     NS_NewRunnableMethod(this, &nsInProcessTabChildGlobal::DelayedDisconnect)
  );
}

void
nsInProcessTabChildGlobal::DelayedDisconnect()
{
  // Don't let the event escape
  mOwner = nullptr;

  // Fire the "unload" event
  nsCOMPtr<nsIDOMEvent> event;
  NS_NewDOMEvent(getter_AddRefs(event), nullptr, nullptr);
  if (event) {
    event->InitEvent(NS_LITERAL_STRING("unload"), false, false);
    event->SetTrusted(true);

    bool dummy;
    nsDOMEventTargetHelper::DispatchEvent(event, &dummy);
  }

  // Continue with the Disconnect cleanup
  nsCOMPtr<nsIDOMWindow> win = do_GetInterface(mDocShell);
  nsCOMPtr<nsPIDOMWindow> pwin = do_QueryInterface(win);
  if (pwin) {
    pwin->SetChromeEventHandler(pwin->GetChromeEventHandler());
  }
  mDocShell = nullptr;
  mChromeMessageManager = nullptr;
  if (mMessageManager) {
    static_cast<nsFrameMessageManager*>(mMessageManager.get())->Disconnect();
    mMessageManager = nullptr;
  }
  if (mListenerManager) {
    mListenerManager->Disconnect();
  }
  
  if (!mLoadingScript) {
    nsContentUtils::ReleaseWrapper(static_cast<nsIDOMEventTarget*>(this),
                                   this);
    if (mCx) {
      DestroyCx();
    }
  } else {
    mDelayedDisconnect = true;
  }
}

NS_IMETHODIMP_(nsIContent *)
nsInProcessTabChildGlobal::GetOwnerContent()
{
  return mOwner;
}

nsresult
nsInProcessTabChildGlobal::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  aVisitor.mCanHandle = true;

  if (mIsBrowserFrame &&
      (!mOwner || !nsContentUtils::IsInChromeDocshell(mOwner->OwnerDoc()))) {
    if (mOwner) {
      nsPIDOMWindow* innerWindow = mOwner->OwnerDoc()->GetInnerWindow();
      if (innerWindow) {
        aVisitor.mParentTarget = innerWindow->GetParentTarget();
      }
    }
  } else {
    aVisitor.mParentTarget = mOwner;
  }

#ifdef DEBUG
  if (mOwner) {
    nsCOMPtr<nsIFrameLoaderOwner> owner = do_QueryInterface(mOwner);
    nsRefPtr<nsFrameLoader> fl = owner->GetFrameLoader();
    if (fl) {
      NS_ASSERTION(this == fl->GetTabChildGlobalAsEventTarget(),
                   "Wrong event target!");
      NS_ASSERTION(fl->mMessageManager == mChromeMessageManager,
                   "Wrong message manager!");
    }
  }
#endif

  return NS_OK;
}

nsresult
nsInProcessTabChildGlobal::InitTabChildGlobal()
{

  nsISupports* scopeSupports =
    NS_ISUPPORTS_CAST(nsIDOMEventTarget*, this);
  NS_ENSURE_STATE(InitTabChildGlobalInternal(scopeSupports));
  return NS_OK;
}

class nsAsyncScriptLoad : public nsRunnable
{
public:
  nsAsyncScriptLoad(nsInProcessTabChildGlobal* aTabChild, const nsAString& aURL)
  : mTabChild(aTabChild), mURL(aURL) {}

  NS_IMETHOD Run()
  {
    mTabChild->LoadFrameScript(mURL);
    return NS_OK;
  }
  nsRefPtr<nsInProcessTabChildGlobal> mTabChild;
  nsString mURL;
};

void
nsInProcessTabChildGlobal::LoadFrameScript(const nsAString& aURL)
{
  if (!nsContentUtils::IsSafeToRunScript()) {
    nsContentUtils::AddScriptRunner(new nsAsyncScriptLoad(this, aURL));
    return;
  }
  if (!mInitialized) {
    mInitialized = true;
    Init();
  }
  bool tmp = mLoadingScript;
  mLoadingScript = true;
  LoadFrameScriptInternal(aURL);
  mLoadingScript = tmp;
  if (!mLoadingScript && mDelayedDisconnect) {
    mDelayedDisconnect = false;
    Disconnect();
  }
}
