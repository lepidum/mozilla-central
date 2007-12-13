/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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

#include "nsIAddressBook.h"
#include "nsAddressBook.h"
#include "nsAbBaseCID.h"
#include "nsDirPrefs.h"
#include "nsIAddrBookSession.h"
#include "nsAbRDFResource.h"
#include "nsIAddrDatabase.h"

#include "xp_core.h"
#include "plstr.h"
#include "prmem.h"
#include "prprf.h"	 

#include "nsCOMPtr.h"
#include "nsIDOMXULElement.h"
#include "nsIRDFCompositeDataSource.h"
#include "nsIRDFResource.h"
#include "nsIRDFService.h"
#include "nsRDFCID.h"
#include "nsIServiceManager.h"
#include "nsIFileLocator.h"
#include "nsFileLocations.h"
#include "nsAppShellCIDs.h"
#include "nsIAppShellService.h"
#include "nsIDOMWindow.h"
#include "nsIContentViewer.h"


static NS_DEFINE_CID(kRDFServiceCID, NS_RDFSERVICE_CID);
static NS_DEFINE_IID(kAppShellServiceCID, NS_APPSHELL_SERVICE_CID);
static NS_DEFINE_CID(kAddressBookDBCID, NS_ADDRDATABASE_CID);
static NS_DEFINE_CID(kFileLocatorCID, NS_FILELOCATOR_CID);
static NS_DEFINE_CID(kAddrBookSessionCID, NS_ADDRBOOKSESSION_CID);
static NS_DEFINE_CID(kAbDirectoryCID, NS_ABDIRECTORY_CID); 
static NS_DEFINE_CID(kAbCardPropertyCID, NS_ABCARDPROPERTY_CID);


const char *kDirectoryDataSourceRoot = "abdirectory://";
const char *kCardDataSourceRoot = "abcard://";



static nsresult ConvertDOMListToResourceArray(nsIDOMNodeList *nodeList, nsISupportsArray **resourceArray)
{
	nsresult rv = NS_OK;
	PRUint32 listLength;
	nsIDOMNode *node;
	nsIDOMXULElement *xulElement;
	nsIRDFResource *resource;

	if(!resourceArray)
		return NS_ERROR_NULL_POINTER;

	if(NS_FAILED(rv = nodeList->GetLength(&listLength)))
		return rv;

	if(NS_FAILED(NS_NewISupportsArray(resourceArray)))
	{
		return NS_ERROR_OUT_OF_MEMORY;
	}

	for(PRUint32 i = 0; i < listLength; i++)
	{
		if(NS_FAILED(nodeList->Item(i, &node)))
			return rv;

		if(NS_SUCCEEDED(rv = node->QueryInterface(nsCOMTypeInfo<nsIDOMXULElement>::GetIID(), (void**)&xulElement)))
		{
			if(NS_SUCCEEDED(rv = xulElement->GetResource(&resource)))
			{
				(*resourceArray)->AppendElement(resource);
				NS_RELEASE(resource);
			}
			NS_RELEASE(xulElement);
		}
		NS_RELEASE(node);
		
	}

	return rv;
}

//
// nsAddressBook
//
nsAddressBook::nsAddressBook()
{
	NS_INIT_REFCNT();
}

nsAddressBook::~nsAddressBook()
{
}

NS_IMPL_ISUPPORTS(nsAddressBook, nsIAddressBook::GetIID());

//
// nsIAddressBook
//

NS_IMETHODIMP nsAddressBook::DeleteCards
(nsIDOMXULElement *tree, nsIDOMXULElement *srcDirectory, nsIDOMNodeList *nodeList)
{
	nsresult rv;

	if(!tree || !srcDirectory || !nodeList)
		return NS_ERROR_NULL_POINTER;

	nsCOMPtr<nsIRDFCompositeDataSource> database;
	nsCOMPtr<nsISupportsArray> resourceArray, dirArray;
	nsCOMPtr<nsIRDFResource> resource;

	rv = srcDirectory->GetResource(getter_AddRefs(resource));

	if(NS_FAILED(rv))
		return rv;

	rv = tree->GetDatabase(getter_AddRefs(database));
	if(NS_FAILED(rv))
		return rv;

	rv = ConvertDOMListToResourceArray(nodeList, getter_AddRefs(resourceArray));
	if(NS_FAILED(rv))
		return rv;

	rv = NS_NewISupportsArray(getter_AddRefs(dirArray));
	if(NS_FAILED(rv))
	{
		return NS_ERROR_OUT_OF_MEMORY;
	}

	dirArray->AppendElement(resource);
	
	rv = DoCommand(database, NC_RDF_DELETE, dirArray, resourceArray);

	return rv;
}

NS_IMETHODIMP nsAddressBook::NewAddressBook
(nsIRDFCompositeDataSource* db, nsIDOMXULElement *srcDirectory, const PRUnichar *name)
{
	if(!db || !srcDirectory || !name)
		return NS_ERROR_NULL_POINTER;

	nsresult rv = NS_OK;
    NS_WITH_SERVICE(nsIRDFService, rdfService, kRDFServiceCID, &rv);
	if(NS_FAILED(rv))
		return rv;

	nsCOMPtr<nsISupportsArray> nameArray, dirArray;

	rv = NS_NewISupportsArray(getter_AddRefs(dirArray));
	if(NS_FAILED(rv))
		return NS_ERROR_OUT_OF_MEMORY;

	nsCOMPtr<nsIRDFResource> parentResource;
	char *parentUri = PR_smprintf("%s", kDirectoryDataSourceRoot);
	rv = rdfService->GetResource(parentUri, getter_AddRefs(parentResource));
	nsCOMPtr<nsIAbDirectory> parentDir = do_QueryInterface(parentResource);
	if (!parentDir)
		return NS_ERROR_NULL_POINTER;
	if (parentUri)
		PR_smprintf_free(parentUri);
	dirArray->AppendElement(parentResource);

	rv = NS_NewISupportsArray(getter_AddRefs(nameArray));
	if(NS_FAILED(rv))
		return NS_ERROR_OUT_OF_MEMORY;
	nsString nameStr(name);
	nsCOMPtr<nsIRDFLiteral> nameLiteral;

	rdfService->GetLiteral(nameStr.GetUnicode(), getter_AddRefs(nameLiteral));
	nameArray->AppendElement(nameLiteral);

	DoCommand(db, "http://home.netscape.com/NC-rdf#NewDirectory", dirArray, nameArray);
	return rv;
}

NS_IMETHODIMP nsAddressBook::DeleteAddressBooks
(nsIRDFCompositeDataSource* db, nsIDOMXULElement *srcDirectory, nsIDOMNodeList *nodeList)
{
	if(!db || !srcDirectory || !nodeList)
		return NS_ERROR_NULL_POINTER;

	nsresult rv = NS_OK;
    NS_WITH_SERVICE(nsIRDFService, rdfService, kRDFServiceCID, &rv);
	if(NS_FAILED(rv))
		return rv;

	nsCOMPtr<nsISupportsArray> dirArray;

	rv = NS_NewISupportsArray(getter_AddRefs(dirArray));
	if(NS_FAILED(rv))
		return NS_ERROR_OUT_OF_MEMORY;

	nsCOMPtr<nsIRDFResource> parentResource;
	char *parentUri = PR_smprintf("%s", kDirectoryDataSourceRoot);
	rv = rdfService->GetResource(parentUri, getter_AddRefs(parentResource));
	nsCOMPtr<nsIAbDirectory> parentDir = do_QueryInterface(parentResource);
	if (!parentDir)
		return NS_ERROR_NULL_POINTER;
	if (parentUri)
		PR_smprintf_free(parentUri);

	dirArray->AppendElement(parentResource);


	nsCOMPtr<nsISupportsArray> resourceArray;

	rv = ConvertDOMListToResourceArray(nodeList, getter_AddRefs(resourceArray));
	if(NS_FAILED(rv))
		return rv;

	DoCommand(db, NC_RDF_DELETE, dirArray, resourceArray);
	return rv;
}

nsresult nsAddressBook::DoCommand(nsIRDFCompositeDataSource* db, char *command,
						nsISupportsArray *srcArray, nsISupportsArray *argumentArray)
{

	nsresult rv;

    NS_WITH_SERVICE(nsIRDFService, rdfService, kRDFServiceCID, &rv);
	if(NS_FAILED(rv))
		return rv;

	nsCOMPtr<nsIRDFResource> commandResource;
	rv = rdfService->GetResource(command, getter_AddRefs(commandResource));
	if(NS_SUCCEEDED(rv))
	{
		rv = db->DoCommand(srcArray, commandResource, argumentArray);
	}

	return rv;

}

NS_IMETHODIMP nsAddressBook::PrintCard()
{
#ifdef DEBUG_seth
  printf("nsAddressBook::PrintCard()\n");
#endif

  nsresult rv = NS_ERROR_FAILURE;
  nsCOMPtr<nsIContentViewer> viewer;

  if (!mWebShell) {
#ifdef DEBUG_seth
        printf("can't print, there is no webshell\n");
#endif
        return rv;
  }

  mWebShell->GetContentViewer(getter_AddRefs(viewer));

  if (viewer) {
    rv = viewer->Print();
  }
#ifdef DEBUG_seth
  else {
        printf("failed to get the viewer for printing\n");
  }
#endif

  return rv;  
}

NS_IMETHODIMP nsAddressBook::PrintAddressbook()
{
#ifdef DEBUG_seth
	printf("nsAddressBook::PrintAddressbook()\n");
#endif
	return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsAddressBook::SetWebShellWindow(nsIDOMWindow *aWin)
{
   NS_PRECONDITION(aWin != nsnull, "null ptr");
   if (!aWin)
       return NS_ERROR_NULL_POINTER;
 
   nsCOMPtr<nsIScriptGlobalObject> globalObj( do_QueryInterface(aWin) );
   if (!globalObj) {
     return NS_ERROR_FAILURE;
   }
 
   nsCOMPtr<nsIWebShell> webShell;
   globalObj->GetWebShell(getter_AddRefs(webShell));
   if (!webShell)
     return NS_ERROR_NOT_INITIALIZED;
 
   mWebShell = webShell;

   return NS_OK;
}


typedef enum
{
	TABFile,
	LDIFFile,
	UnknownFile
} ImportFileType;

class AddressBookParser 
{
protected:

    nsCAutoString	mLine;
	nsIFileSpecWithUI*	mFileSpec;
	char*			mDbUri;
	nsCOMPtr<nsIAddrDatabase> mDatabase;  
	PRInt32	mFileType;

    nsresult ParseTabFile();
    nsresult ParseLdifFile();
	void AddTabRowToDatabase();
	void AddLdifRowToDatabase();
	void AddLdifColToDatabase(nsIMdbRow* newRow, char* typeSlot, char* valueSlot);

	nsresult GetLdifStringRecord(char* buf, PRInt32 len, PRInt32* stopPos);
	nsresult str_parse_line(char *line, char	**type, char **value, int *vlen);
	char * str_getline( char **next );

public:
    AddressBookParser(nsIFileSpecWithUI* fileSpec);
    ~AddressBookParser();

    nsresult ParseFile();
};

AddressBookParser::AddressBookParser(nsIFileSpecWithUI* fileSpec)
{
	mFileSpec = fileSpec;
	mDbUri = nsnull;
	mFileType = UnknownFile;
}

AddressBookParser::~AddressBookParser(void)
{
	if(mDbUri)
		PR_smprintf_free(mDbUri);
	if (mDatabase)
	{
		mDatabase->Close(PR_TRUE);
		mDatabase = null_nsCOMPtr();
	}
}

const char *kTabExtension = ".txt";  
const char *kLdifExtension = ".ldi";  

nsresult AddressBookParser::ParseFile()
{
	/* Get database file name */
	char *leafName = nsnull;
	nsString fileString;
	if (mFileSpec)
	{
		mFileSpec->GetLeafName(&leafName);
		fileString = leafName;
		if (-1 != fileString.Find(kTabExtension))
			mFileType = TABFile;
		else if (-1 != fileString.Find(kLdifExtension))
			mFileType = LDIFFile;
		else
			return NS_ERROR_FAILURE;

		PRInt32 i = 0;
		while (leafName[i] != '\0')
		{
			if (leafName[i] == '.')
			{
				leafName[i] = '\0';
				break;
			}
			else
				i++;
		}
		if (leafName)
			mDbUri = PR_smprintf("%s%s.mab", kDirectoryDataSourceRoot, leafName);
	}

	nsresult rv = NS_OK;
	nsFileSpec* dbPath = nsnull;
	char* fileName = PR_smprintf("%s.mab", leafName);

	NS_WITH_SERVICE(nsIAddrBookSession, abSession, kAddrBookSessionCID, &rv); 
	if(NS_SUCCEEDED(rv))
		abSession->GetUserProfileDirectory(&dbPath);
	
	/* create address book database  */
	if (dbPath)
	{
		(*dbPath) += fileName;
		NS_WITH_SERVICE(nsIAddrDatabase, addrDBFactory, kAddressBookDBCID, &rv);
		if (NS_SUCCEEDED(rv) && addrDBFactory)
			rv = addrDBFactory->Open(dbPath, PR_TRUE, getter_AddRefs(mDatabase), PR_TRUE);
	}

	if (NS_FAILED(rv))
        return NS_ERROR_FAILURE;

    // Initialize the parser for a run...
    mLine.Truncate();

	if (mFileType == TABFile)
		rv = ParseTabFile();
	if (mFileType == LDIFFile)
		rv = ParseLdifFile();

	if(NS_FAILED(rv))
		return rv;

    NS_WITH_SERVICE(nsIRDFService, rdfService, kRDFServiceCID, &rv);
	if(NS_FAILED(rv))
		return rv;
	nsCOMPtr<nsIRDFResource> parentResource;
	char *parentUri = PR_smprintf("%s", kDirectoryDataSourceRoot);
	rv = rdfService->GetResource(parentUri, getter_AddRefs(parentResource));
	nsCOMPtr<nsIAbDirectory> parentDir = do_QueryInterface(parentResource);
	if (!parentDir)
		return NS_ERROR_NULL_POINTER;
	if (parentUri)
		PR_smprintf_free(parentUri);

	parentDir->CreateNewDirectory(fileString.GetUnicode(), fileName);

	if (leafName)
		nsCRT::free(leafName);
	if (fileName)
		PR_smprintf_free(fileName);
   return NS_OK;
}

nsresult AddressBookParser::ParseTabFile()
{
    char buf[1024];
	char* pBuf = &buf[0];
    PRInt32 len = 0;
	PRBool bEof = PR_FALSE;

	while (NS_SUCCEEDED(mFileSpec->Eof(&bEof)) && !bEof)
	{
		if (NS_SUCCEEDED(mFileSpec->Read(&pBuf, (PRInt32)sizeof(buf), &len)) && len > 0)
		{
			for (PRInt32 i = 0; i < len; i++) 
			{
				char c = buf[i];
				if (c != '\r' && c != '\n')
				{
         			mLine.Append(c);
				}
				else
				{
					if (mLine.Length())
					{
						if (mDatabase)
						{
							AddTabRowToDatabase();
						}
					}
				}
			}
		}
	}
	return NS_OK;
}

#define POS_DISPLAY_NAME	1
#define POS_LAST_NAME 		2
#define POS_FIRST_NAME 		3
#define POS_NOTE 			4
#define POS_CITY 			5
#define POS_STATE 			6
#define POS_EMAIL 			7
#define POS_TITLE 			8
#define POS_UNKNOWN 		9
#define POS_ADDRESS 		10
#define POS_ZIP 			11
#define POS_COUNTRY 		12
#define POS_WORK_NUMBER 	13
#define POS_FAX_NUMBER 		14
#define POS_HOME_NUMBER 	15
#define POS_ORGANIZATION 	16
#define POS_NICK_NAME 		17
#define POS_CELLULAR_NUMBER	18
#define POS_PAGE_NUMBER		19

void AddressBookParser::AddTabRowToDatabase()
{
    nsCAutoString column;
	
	nsIMdbRow* newRow;
	mDatabase->GetNewRow(&newRow); 

	if (!newRow)
		return;

	int nCol = 0;
	int nSize = mLine.Length();

	for (int i = 0; i < nSize; i++)
	{
        char c = (mLine.GetBuffer())[i];
		while (c != '\t')
		{
			column.Append(c);
			c = (mLine.GetBuffer()) [++i];
		}
		
		nCol += 1;
 
		switch (nCol)
		{
		case POS_FIRST_NAME:
			if (column.Length() > 0)
				mDatabase->AddFirstName(newRow, column);
			break;
		case POS_LAST_NAME:
			if (column.Length() > 0)
				mDatabase->AddLastName(newRow, column);
			break;
		case POS_DISPLAY_NAME:
			if (column.Length() > 0)
				mDatabase ->AddDisplayName(newRow, column);
			break;
		case POS_NICK_NAME:
			if (column.Length() > 0)
				mDatabase->AddNickName(newRow, column);
			break;
		case POS_EMAIL:
			if (column.Length() > 0)
        		mDatabase->AddPrimaryEmail(newRow, column);
			break;
		case POS_WORK_NUMBER:
			if (column.Length() > 0)
        		mDatabase->AddWorkPhone(newRow, column);
			break;
		case POS_HOME_NUMBER:
			if (column.Length() > 0)
        		mDatabase->AddHomePhone(newRow, column);
			break;
		case POS_PAGE_NUMBER:
			if (column.Length() > 0)
        		mDatabase->AddPagerNumber(newRow, column);
			break;
		case POS_CELLULAR_NUMBER:
			if (column.Length() > 0)
        		mDatabase->AddCellularNumber(newRow, column);
			break;
		case POS_FAX_NUMBER:
			if (column.Length() > 0)
        		mDatabase->AddFaxNumber(newRow, column);
			break;
		case POS_ADDRESS:
			if (column.Length() > 0)
        		mDatabase->AddWorkAddress(newRow, column);
			break;
		case POS_CITY:
			if (column.Length() > 0)
        		mDatabase->AddWorkCity(newRow, column);
			break;
		case POS_STATE:
			if (column.Length() > 0)
        		mDatabase->AddWorkState(newRow, column);
			break;
		case POS_ZIP:
			if (column.Length() > 0)
        		mDatabase->AddWorkZipCode(newRow, column);
			break;
		case POS_COUNTRY:
			if (column.Length() > 0)
        		mDatabase->AddWorkCountry(newRow, column);
			break;
		case POS_TITLE:
			if (column.Length() > 0)
        		mDatabase->AddJobTitle(newRow, column);
			break;
		case POS_ORGANIZATION:
			if (column.Length() > 0)
        		mDatabase->AddDepartment(newRow, column);
			break;
		case POS_NOTE:
			if (column.Length() > 0)
        		mDatabase->AddNotes(newRow, column);
			break;
		case POS_UNKNOWN:
		default:
			break;
		}

		if (column.Length() > 0)
			column.Truncate();
	}
	if (mLine.Length() > 0)
		mLine.Truncate();
	if (nSize)
		mDatabase->AddCardRowToDB(newRow);
}

#define RIGHT2			0x03
#define RIGHT4			0x0f
#define CONTINUED_LINE_MARKER	'\001'
#define IS_SPACE(VAL)                \
    (((((intn)(VAL)) & 0x7f) == ((intn)(VAL))) && isspace((intn)(VAL)) )

static unsigned char b642nib[0x80] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x3e, 0xff, 0xff, 0xff, 0x3f,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
	0x3c, 0x3d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
	0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
	0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff
};

/*
 * str_parse_line - takes a line of the form "type:[:] value" and splits it
 * into components "type" and "value".  if a double colon separates type from
 * value, then value is encoded in base 64, and parse_line un-decodes it
 * (in place) before returning.
 */

nsresult AddressBookParser::str_parse_line(
    char	*line,
    char	**type,
    char	**value,
    int		*vlen
)
{
	char	*p, *s, *d, *byte, *stop;
	char	nib;
	int	i, b64;

	/* skip any leading space */
	while ( IS_SPACE( *line ) ) {
		line++;
	}
	*type = line;

	for ( s = line; *s && *s != ':'; s++ )
		;	/* NULL */
	if ( *s == '\0' ) {
		return NS_ERROR_FAILURE;
	}

	/* trim any space between type and : */
	for ( p = s - 1; p > line && isspace( *p ); p-- ) {
		*p = '\0';
	}
	*s++ = '\0';

	/* check for double : - indicates base 64 encoded value */
	if ( *s == ':' ) {
		s++;
		b64 = 1;

	/* single : - normally encoded value */
	} else {
		b64 = 0;
	}

	/* skip space between : and value */
	while ( IS_SPACE( *s ) ) {
		s++;
	}

	/* if no value is present, error out */
	if ( *s == '\0' ) {
		return NS_ERROR_FAILURE;
	}

	/* check for continued line markers that should be deleted */
	for ( p = s, d = s; *p; p++ ) {
		if ( *p != CONTINUED_LINE_MARKER )
			*d++ = *p;
	}
	*d = '\0';

	*value = s;
	if ( b64 ) {
		stop = PL_strchr( s, '\0' );
		byte = s;
		for ( p = s, *vlen = 0; p < stop; p += 4, *vlen += 3 ) {
			for ( i = 0; i < 3; i++ ) {
				if ( p[i] != '=' && (p[i] & 0x80 ||
				    b642nib[ p[i] & 0x7f ] > 0x3f) ) {
					return NS_ERROR_FAILURE;
				}
			}

			/* first digit */
			nib = b642nib[ p[0] & 0x7f ];
			byte[0] = nib << 2;
			/* second digit */
			nib = b642nib[ p[1] & 0x7f ];
			byte[0] |= nib >> 4;
			byte[1] = (nib & RIGHT4) << 4;
			/* third digit */
			if ( p[2] == '=' ) {
				*vlen += 1;
				break;
			}
			nib = b642nib[ p[2] & 0x7f ];
			byte[1] |= nib >> 2;
			byte[2] = (nib & RIGHT2) << 6;
			/* fourth digit */
			if ( p[3] == '=' ) {
				*vlen += 2;
				break;
			}
			nib = b642nib[ p[3] & 0x7f ];
			byte[2] |= nib;

			byte += 3;
		}
		s[ *vlen ] = '\0';
	} else {
		*vlen = (int) (d - s);
	}

	return NS_OK;
}

/*
 * str_getline - return the next "line" (minus newline) of input from a
 * string buffer of lines separated by newlines, terminated by \n\n
 * or \0.  this routine handles continued lines, bundling them into
 * a single big line before returning.  if a line begins with a white
 * space character, it is a continuation of the previous line. the white
 * space character (nb: only one char), and preceeding newline are changed
 * into CONTINUED_LINE_MARKER chars, to be deleted later by the
 * str_parse_line() routine above.
 *
 * it takes a pointer to a pointer to the buffer on the first call,
 * which it updates and must be supplied on subsequent calls.
 */

char * AddressBookParser::str_getline( char **next )
{
	char	*lineStr;
	char	c;

	if ( *next == NULL || **next == '\n' || **next == '\0' ) {
		return( NULL );
	}

	lineStr = *next;
	while ( (*next = PL_strchr( *next, '\n' )) != NULL ) {
		c = *(*next + 1);
		if ( IS_SPACE ( c ) && c != '\n' ) {
			**next = CONTINUED_LINE_MARKER;
			*(*next+1) = CONTINUED_LINE_MARKER;
		} else {
			*(*next)++ = '\0';
			break;
		}
	}

	return( lineStr );
}

/*
 * get one ldif record
 * 
 */
nsresult AddressBookParser::GetLdifStringRecord(char* buf, PRInt32 len, PRInt32* stopPos)
{
	PRInt32 LFCount = 0;
	PRInt32 CRCount = 0;

	for (; *stopPos < len; (*stopPos)++) 
	{
		char c = buf[*stopPos];

		if (c == 0xA)
		{
			LFCount++;
		}
		else if (c == 0xD)
		{
			CRCount++;
		}
		else if ( c != 0xA && c != 0xD)
		{
			if (LFCount == 0 && CRCount == 0)
         		mLine.Append(c);
			else if (( LFCount > 1) || ( CRCount > 2 && LFCount ) ||
				( !LFCount && CRCount > 1 ))
			{
				return NS_OK;
			}
			else if ((LFCount == 1 || CRCount == 1) && c != ' ')
			{
         		mLine.Append('\n');
         		mLine.Append(c);
				LFCount = 0;
				CRCount = 0;
			}
		}
	}
	if (*stopPos ==len && (LFCount > 1) || (CRCount > 2 && LFCount) ||
		(!LFCount && CRCount > 1))
		return NS_OK;
	else
		return NS_ERROR_FAILURE;
}

nsresult AddressBookParser::ParseLdifFile()
{
    char buf[1024];
	char* pBuf = &buf[0];
	PRInt32 startPos = 0;
    PRInt32 len = 0;
	PRBool bEof = PR_FALSE;

	while (NS_SUCCEEDED(mFileSpec->Eof(&bEof)) && !bEof)
	{
		if (NS_SUCCEEDED(mFileSpec->Read(&pBuf, (PRInt32)sizeof(buf), &len)) && len > 0)
		{
			startPos = 0;

			while (NS_SUCCEEDED(GetLdifStringRecord(buf, len, &startPos)))
			{
				AddLdifRowToDatabase();
			}
		}
	}
	//last row
	if (mLine.Length() > 0)
		AddLdifRowToDatabase(); 
	return NS_OK;
}

void AddressBookParser::AddLdifRowToDatabase()
{
	nsIMdbRow* newRow = nsnull;
	if (mDatabase)
	{
		mDatabase->GetNewRow(&newRow); 

		if (!newRow)
			return;
	}
	else
		return;

	char* cursor = (char*)mLine.ToNewCString(); 
	char* saveCursor = cursor;  /* keep for deleting */ 
	char* line = 0; 
	char* typeSlot = 0; 
	char* valueSlot = 0; 
	int length = 0;  // the length  of an ldif attribute
	while ( (line = str_getline(&cursor)) != NULL)
	{
		if ( str_parse_line(line, &typeSlot, &valueSlot, &length) == 0 )
		{
			AddLdifColToDatabase(newRow, typeSlot, valueSlot);
		}
		else
			continue; // parse error: continue with next loop iteration
	}
	delete [] saveCursor;
	mDatabase->AddCardRowToDB(newRow);	

	if (mLine.Length() > 0)
		mLine.Truncate();
}

void AddressBookParser::AddLdifColToDatabase(nsIMdbRow* newRow, char* typeSlot, char* valueSlot)
{
    nsCAutoString colType(typeSlot);
    nsCAutoString column(valueSlot);

//	mdb_u1 firstByte = (mdb_u1)colType[0];
	mdb_u1 firstByte = (mdb_u1)(colType.GetBuffer())[0];
	switch ( firstByte )
	{
	case 'b':
	  if ( -1 != colType.Find("birthyear") )
		mDatabase->AddBirthYear(newRow, column);
	  break; // 'b'

	case 'c':
	  if ( -1 != colType.Find("cn") || -1 != colType.Find("commonname") )
		mDatabase->AddDisplayName(newRow, column);

	  else if ( -1 != colType.Find("countryName") )
		mDatabase->AddWorkCountry(newRow, column);

	  // else if ( -1 != colType.Find("charset") )
	  //   ioRow->AddColumn(ev, this->ColCharset(), yarn);

	  else if ( -1 != colType.Find("cellphone") )
		mDatabase->AddCellularNumber(newRow, column);

//		  else if ( -1 != colType.Find("calendar") )
//			ioRow->AddColumn(ev, this->ColCalendar(), yarn);

//		  else if ( -1 != colType.Find("car") )
//			ioRow->AddColumn(ev, this->ColCar(), yarn);

	  else if ( -1 != colType.Find("carphone") )
		mDatabase->AddCellularNumber(newRow, column);
//			ioRow->AddColumn(ev, this->ColCarPhone(), yarn);

//		  else if ( -1 != colType.Find("carlicense") )
//			ioRow->AddColumn(ev, this->ColCarLicense(), yarn);
        
	  else if ( -1 != colType.Find("custom1") )
		mDatabase->AddCustom1(newRow, column);
        
	  else if ( -1 != colType.Find("custom2") )
		mDatabase->AddCustom2(newRow, column);
        
	  else if ( -1 != colType.Find("custom3") )
		mDatabase->AddCustom3(newRow, column);
        
	  else if ( -1 != colType.Find("custom4") )
		mDatabase->AddCustom4(newRow, column);
        
	  else if ( -1 != colType.Find("company") )
		mDatabase->AddCompany(newRow, column);
	  break; // 'c'

	case 'd':
	  if ( -1 != colType.Find("description") )
		mDatabase->AddNotes(newRow, column);

//		  else if ( -1 != colType.Find("dn") ) // distinuished name
//			ioRow->AddColumn(ev, this->ColDistName(), yarn);

	  else if ( -1 != colType.Find("department") )
		mDatabase->AddDepartment(newRow, column);

//		  else if ( -1 != colType.Find("departmentnumber") )
//			ioRow->AddColumn(ev, this->ColDepartmentNumber(), yarn);

//		  else if ( -1 != colType.Find("date") )
//			ioRow->AddColumn(ev, this->ColDate(), yarn);
	  break; // 'd'

	case 'e':

//		  if ( -1 != colType.Find("employeeid") )
//			ioRow->AddColumn(ev, this->ColEmployeeId(), yarn);

//		  else if ( -1 != colType.Find("employeetype") )
//			ioRow->AddColumn(ev, this->ColEmployeeType(), yarn);
	  break; // 'e'

	case 'f':

	  if ( -1 != colType.Find("fax") ||
		-1 != colType.Find("facsimiletelephonenumber") )
		mDatabase->AddFaxNumber(newRow, column);
	  break; // 'f'

	case 'g':
	  if ( -1 != colType.Find("givenname") )
		mDatabase->AddFirstName(newRow, column);

//		  else if ( -1 != colType.Find("gif") )
//			ioRow->AddColumn(ev, this->ColGif(), yarn);

//		  else if ( -1 != colType.Find("geo") )
//			ioRow->AddColumn(ev, this->ColGeo(), yarn);

	  break; // 'g'

	case 'h':
	  if ( -1 != colType.Find("homephone") )
		mDatabase->AddHomePhone(newRow, column);

	  else if ( -1 != colType.Find("homeurl") )
		mDatabase->AddWebPage1(newRow, column);
	  break; // 'h'

	case 'i':
//		  if ( -1 != colType.Find("imapurl") )
//			ioRow->AddColumn(ev, this->ColImapUrl(), yarn);
	  break; // 'i'

	case 'j':
//		  if ( -1 != colType.Find("jpeg") || -1 != colType.Find("jpegfile") )
//			ioRow->AddColumn(ev, this->ColJpegFile(), yarn);

	  break; // 'j'

	case 'k':
//		  if ( -1 != colType.Find("key") )
//			ioRow->AddColumn(ev, this->ColKey(), yarn);

//		  else if ( -1 != colType.Find("keywords") )
//			ioRow->AddColumn(ev, this->ColKeywords(), yarn);

	  break; // 'k'

	case 'l':
	  if ( -1 != colType.Find("l") || -1 != colType.Find("locality") )
		mDatabase->AddWorkCity(newRow, column);

//		  else if ( -1 != colType.Find("language") )
//			ioRow->AddColumn(ev, this->ColLanguage(), yarn);

//		  else if ( -1 != colType.Find("logo") )
//			ioRow->AddColumn(ev, this->ColLogo(), yarn);

//		  else if ( -1 != colType.Find("location") )
//			ioRow->AddColumn(ev, this->ColLocation(), yarn);

	  break; // 'l'

	case 'm':
	  if ( -1 != colType.Find("mail") )
		mDatabase->AddPrimaryEmail(newRow, column);

//		  else if ( -1 != colType.Find("member") && list )
//		  {
//			this->add-list-member(list, yarn); // see also "uniquemember"
//		  }

//		  else if ( -1 != colType.Find("manager") )
//			ioRow->AddColumn(ev, this->ColManager(), yarn);

//		  else if ( -1 != colType.Find("modem") )
//			ioRow->AddColumn(ev, this->ColModem(), yarn);

//		  else if ( -1 != colType.Find("msgphone") )
//			ioRow->AddColumn(ev, this->ColMessagePhone(), yarn);

	  break; // 'm'

	case 'n':
//		  if ( -1 != colType.Find("note") )
//			ioRow->AddColumn(ev, this->ColNote(), yarn);

	  if ( -1 != colType.Find("notes") )
		mDatabase->AddNotes(newRow, column);

//		  else if ( -1 != colType.Find("n") )
//			ioRow->AddColumn(ev, this->ColN(), yarn);

//		  else if ( -1 != colType.Find("notifyurl") )
//			ioRow->AddColumn(ev, this->ColNotifyUrl(), yarn);

	  break; // 'n'

	case 'o':
//		  if ( -1 != colType.Find("o") ) // organization
//			ioRow->AddColumn(ev, this->ColCompany(), yarn);

//		  else if ( -1 != colType.Find("objectclass") ) 
//		  {
//			if ( strcasecomp(inVal, "person") ) // objectclass == person?
//			{
//			  this->put-bool-row-col(row, this->ColPerson(), mdbBool_kTrue);
//			}
//			else if ( strcasecomp(inVal, "groupofuniquenames") || 
//			  strcasecomp(inVal, "groupOfNames") ) // objectclass == list?
//			{
//			  this->put-bool-row-col(row, this->ColPerson(), mdbBool_kFalse);
//			  isList = mdbBool_kTrue;
//			  if ( !list )
//				list = this->make-new-mdb-list-table-for-row(ioRow);
//			}
//		  }

//		  else if ( -1 != colType.Find("ou") || -1 != colType.Find("orgunit") )
//			ioRow->AddColumn(ev, this->ColDepartment(), yarn);

	  break; // 'o'

	case 'p':
	  if ( -1 != colType.Find("postalcode") )
		mDatabase->AddWorkZipCode(newRow, column);

	  else if ( -1 != colType.Find("postOfficeBox") )
		mDatabase->AddWorkAddress(newRow, column);

	  else if ( -1 != colType.Find("pager") ||
		-1 != colType.Find("pagerphone") )
		mDatabase->AddPagerNumber(newRow, column);
                    
//		  else if ( -1 != colType.Find("photo") )
//			ioRow->AddColumn(ev, this->ColPhoto(), yarn);

//		  else if ( -1 != colType.Find("parentphone") )
//			ioRow->AddColumn(ev, this->ColParentPhone(), yarn);

//		  else if ( -1 != colType.Find("pageremail") )
//			ioRow->AddColumn(ev, this->ColPagerEmail(), yarn);

//		  else if ( -1 != colType.Find("prefurl") )
//			ioRow->AddColumn(ev, this->ColPrefUrl(), yarn);

//		  else if ( -1 != colType.Find("priority") )
//			ioRow->AddColumn(ev, this->ColPriority(), yarn);

	  break; // 'p'

	case 'r':
	  if ( -1 != colType.Find("region") )
		mDatabase->AddWorkState(newRow, column);

//		  else if ( -1 != colType.Find("rfc822mailbox") )
//			ioRow->AddColumn(ev, this->ColPrimaryEmail(), yarn);

//		  else if ( -1 != colType.Find("rev") )
//			ioRow->AddColumn(ev, this->ColRev(), yarn);

//		  else if ( -1 != colType.Find("role") )
//			ioRow->AddColumn(ev, this->ColRole(), yarn);
	  break; // 'r'

	case 's':
	  if ( -1 != colType.Find("sn") || -1 != colType.Find("surname") )
		mDatabase->AddLastName(newRow, column);

	  else if ( -1 != colType.Find("st") )
		mDatabase->AddWorkState(newRow, column);

	  else if ( -1 != colType.Find("streetaddress") )
		mDatabase->AddWorkAddress2(newRow, column);

//		  else if ( -1 != colType.Find("secretary") )
//			ioRow->AddColumn(ev, this->ColSecretary(), yarn);

//		  else if ( -1 != colType.Find("sound") )
//			ioRow->AddColumn(ev, this->ColSound(), yarn);

//		  else if ( -1 != colType.Find("sortstring") )
//			ioRow->AddColumn(ev, this->ColSortString(), yarn);
        
	  break; // 's'

	case 't':
	  if ( -1 != colType.Find("title") )
		mDatabase->AddJobTitle(newRow, column);

	  else if ( -1 != colType.Find("telephonenumber") )
		mDatabase->AddWorkPhone(newRow, column);

//		  else if ( -1 != colType.Find("tiff") )
//			ioRow->AddColumn(ev, this->ColTiff(), yarn);

//		  else if ( -1 != colType.Find("tz") )
//			ioRow->AddColumn(ev, this->ColTz(), yarn);
	  break; // 't'

	case 'u':

//		  if ( -1 != colType.Find("uniquemember") && list )
//		  {
//			this->add-list-member(list, yarn); // see also "member"
//		  }

//		  else if ( -1 != colType.Find("uid") )
//			ioRow->AddColumn(ev, this->ColUid(), yarn);

	  break; // 'u'

	case 'v':
//		  if ( -1 != colType.Find("version") )
//			ioRow->AddColumn(ev, this->ColVersion(), yarn);

//		  else if ( -1 != colType.Find("voice") )
//			ioRow->AddColumn(ev, this->ColVoice(), yarn);

	  break; // 'v'

	case 'w':
	  if ( -1 != colType.Find("workurl") )
		mDatabase->AddWebPage2(newRow, column);

	  break; // 'w'

	case 'x':
	  if ( -1 != colType.Find("xmozillanickname") )
		mDatabase->AddNickName(newRow, column);

	  else if ( -1 != colType.Find("xmozillausehtmlmail") )
	  {
		; //add use plain text
	  }

	  break; // 'x'

	case 'z':
	  if ( -1 != colType.Find("zip") ) // alias for postalcode
		mDatabase->AddWorkZipCode(newRow, column);

	  break; // 'z'

	default:
	  break; // default
	}
}

NS_IMETHODIMP nsAddressBook::ImportAddressBook()
{
    nsresult rv = NS_ERROR_FAILURE;

	nsCOMPtr<nsIFileSpecWithUI> fileSpec(getter_AddRefs(NS_CreateFileSpecWithUI()));
	if (!fileSpec)
		return NS_ERROR_FAILURE;

	rv = fileSpec->ChooseInputFile(
	"Open File", nsIFileSpecWithUI::eAllFiles, nsnull, nsnull);
	if (NS_FAILED(rv))
		return rv;

	rv = fileSpec->OpenStreamForReading();
	if (NS_SUCCEEDED(rv))
	{
		AddressBookParser abParser(fileSpec);
		rv = abParser.ParseFile();
		rv = fileSpec->CloseStream();
	}

	return rv;
}

