/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */
#ifndef nsIHTMLDocument_h___
#define nsIHTMLDocument_h___

#include "nsISupports.h"
class nsIImageMap;
class nsString;
class nsIFormManager;
class nsIHTMLStyleSheet;

/* b2a848b0-d0a9-11d1-89b1-006008911b81 */
#define NS_IHTMLDOCUMENT_IID \
{0xb2a848b0, 0xd0a9, 0x11d1, {0x89, 0xb1, 0x00, 0x60, 0x08, 0x91, 0x1b, 0x81}}

enum nsDTDMode {
  eDTDMode_NoQuirks = 0,
  eDTDMode_Nav = 1,
  eDTDMode_Other = 2
};

/**
 * HTML document extensions to nsIDocument.
 */
class nsIHTMLDocument : public nsISupports {
public:
  NS_IMETHOD SetTitle(const nsString& aTitle) = 0;

  NS_IMETHOD AddImageMap(nsIImageMap* aMap) = 0;

  NS_IMETHOD GetImageMap(const nsString& aMapName, nsIImageMap** aResult) = 0;

  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  // XXX Temporary form methods. Forms will soon become actual content
  // elements. For now, the document keeps a list of them.
  NS_IMETHOD AddForm(nsIFormManager *aForm) = 0;

  NS_IMETHOD_(PRInt32) GetFormCount() const = 0;
  
  NS_IMETHOD GetFormAt(PRInt32 aIndex, nsIFormManager **aForm) const = 0;
  // XXX
  // XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 
  NS_IMETHOD GetAttributeStyleSheet(nsIHTMLStyleSheet** aStyleSheet) = 0;

  /**
   * Access DTD compatibility mode for this document
   */
  NS_IMETHOD GetDTDMode(nsDTDMode& aMode) = 0;
  NS_IMETHOD SetDTDMode(nsDTDMode aMode) = 0;

};

#endif /* nsIHTMLDocument_h___ */
