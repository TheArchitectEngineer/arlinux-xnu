/* Transport-specific token replacement for a genuinely local Linux scene.
 * The original token constructor requires a remote BSServiceConnectionEndpoint.
 * Local tokens retain immutable identity/copy/equality semantics, expose no
 * remote endpoint and cannot be encoded as Apple service capabilities.
 * Factory ownership: guest main loop; return value is autoreleased.
 */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

static Class local_token_class;
static char identifier_key,workspace_key,description_key;
static id token_identifier(id self,SEL command)
{ (void)command; return objc_getAssociatedObject(self,&identifier_key); }
static id token_workspace(id self,SEL command)
{ (void)command; return objc_getAssociatedObject(self,&workspace_key); }
static id token_description(id self,SEL command)
{ (void)command; return objc_getAssociatedObject(self,&description_key); }
static id token_endpoint(id self,SEL command)
{ (void)self; (void)command; return nil; }
static id token_copy(id self,SEL command,NSZone *zone)
{ (void)command; (void)zone; return [self retain]; }
static NSUInteger token_hash(id self,SEL command)
{ return [token_description(self,command) hash]; }
static BOOL token_equal(id self,SEL command,id other)
{
    return self==other || ([other isKindOfClass:local_token_class] &&
        [token_identifier(self,command) isEqual:token_identifier(other,command)] &&
        [token_workspace(self,command) isEqual:token_workspace(other,command)]);
}
static void token_encode(id self,SEL command,id coder)
{
    (void)self; (void)coder;
    fprintf(stderr,"IOS_LIFECYCLE_UNSUPPORTED operation=local-token-remote-encoding selector=%s\n",sel_getName(command));
    abort();
}
static BOOL token_secure(id self,SEL command)
{ (void)self; (void)command; return NO; }
static void add_method(Class cls,const char *name,IMP implementation,const char *types)
{
    if (!class_addMethod(cls,sel_registerName(name),implementation,types)) abort();
}
id arlinux_ios_scene_token(NSString *identifier,NSString *workspace)
{
    if (!identifier.length || !workspace.length) abort();
    if (!local_token_class) {
        Class base=objc_getClass("FBSSceneIdentityToken");
        if (!base) abort();
        Class cls=objc_allocateClassPair(base,"ARLinuxLocalSceneIdentityToken",0);
        if (!cls) abort();
        add_method(cls,"identifier",(IMP)token_identifier,"@16@0:8");
        add_method(cls,"workspace",(IMP)token_workspace,"@16@0:8");
        add_method(cls,"workspaceIdentifier",(IMP)token_workspace,"@16@0:8");
        add_method(cls,"stringRepresentation",(IMP)token_description,"@16@0:8");
        add_method(cls,"description",(IMP)token_description,"@16@0:8");
        add_method(cls,"debugDescription",(IMP)token_description,"@16@0:8");
        add_method(cls,"endpoint",(IMP)token_endpoint,"@16@0:8");
        add_method(cls,"copyWithZone:",(IMP)token_copy,"@24@0:8^{_NSZone=}16");
        add_method(cls,"hash",(IMP)token_hash,"Q16@0:8");
        add_method(cls,"isEqual:",(IMP)token_equal,"B24@0:8@16");
        for (const char **name=(const char *[]){"encodeWithCoder:","encodeWithBSXPCCoder:","encodeWithXPCDictionary:",NULL};*name;name++)
            add_method(cls,*name,(IMP)token_encode,"v24@0:8@16");
        add_method(object_getClass(cls),"supportsSecureCoding",(IMP)token_secure,"B16@0:8");
        add_method(object_getClass(cls),"supportsBSXPCSecureCoding",(IMP)token_secure,"B16@0:8");
        objc_registerClassPair(cls);local_token_class=cls;
    }
    id token=class_createInstance(local_token_class,0);
    if (!token) abort();
    /* Initialize NSObject state without invoking the remote-token constructor.
     * Base token storage remains zero; its deallocation releases only nils. */
    IMP initialize=class_getMethodImplementation([NSObject class],@selector(init));
    token=((id (*)(id,SEL))initialize)(token,@selector(init));
    objc_setAssociatedObject(token,&identifier_key,identifier,OBJC_ASSOCIATION_COPY_NONATOMIC);
    objc_setAssociatedObject(token,&workspace_key,workspace,OBJC_ASSOCIATION_COPY_NONATOMIC);
    NSString *description=[NSString stringWithFormat:@"arlinux-local-scene:%@/%@",workspace,identifier];
    objc_setAssociatedObject(token,&description_key,description,OBJC_ASSOCIATION_COPY_NONATOMIC);
    return [token autorelease];
}
