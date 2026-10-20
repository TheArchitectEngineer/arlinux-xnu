/* Production ABI imports for the native iOS platform adapter.
 * Loader code only binds imports; UIKit policy/drawing lives in the adapter.
 * Data imports are dereferenced, function imports require executable mappings.
 * These are linkage requirements, never a functional acceptance checklist.
 */
#pragma once
#include <stddef.h>
#include "ios-framework-api.h"
struct ios_framework_import { const char *image,*symbol; unsigned int offset,data; };
#define IOS_IMPORT_ROW(image,symbol,field,data) {image,symbol,offsetof(struct darwin_ui_api,field),data},
static const struct ios_framework_import ios_framework_imports[]={IOS_UI_IMPORTS(IOS_IMPORT_ROW)};
#undef IOS_IMPORT_ROW
static int ios_framework_image(const char *image)
{
    for (unsigned int i=0;i<sizeof(ios_framework_imports)/sizeof(*ios_framework_imports);i++)
        if (!strcmp(image,ios_framework_imports[i].image)) return 1;
    return 0;
}
static void ios_framework_bind(struct darwin_ui_api *api,const char *image,const char *symbol,uint64_t address)
{
    for (unsigned int i=0;i<sizeof(ios_framework_imports)/sizeof(*ios_framework_imports);i++) {
        const struct ios_framework_import *entry=ios_framework_imports+i;
        if (strcmp(image,entry->image) || strcmp(symbol,entry->symbol)) continue;
        need(cache_contains(address,entry->data ? 8 : 4,entry->data ? 1 : 4),"iOS framework import address/protection");
        uint64_t value=address;
        if (entry->data) memcpy(&value,(void *)address,8);
        memcpy((unsigned char *)api+entry->offset,&value,8);
        fprintf(stderr,"NATIVE_UI_API symbol=%s address=%#" PRIx64 "\n",symbol,address);
        return;
    }
}
