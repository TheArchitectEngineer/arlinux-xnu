/* Selected legacy iOS scene wire profile. Pure serialization: no registry,
 * process lookup, port ownership, guest function calls or display fallback.
 * The registry supplies verified geometry and owns delivery/reply lifetimes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ios-scene-codec.h"
#include "array.h"
static spc_dictionary_t *session_encoded_object(const char *class_name)
{
    spc_dictionary_t *object=spc_dictionary_create();
    spc_dictionary_set_string(object,"bs_encodedObjectClassName",class_name);
    return object;
}

static void session_dictionary_object(spc_dictionary_t *dictionary,const char *key,spc_dictionary_t *object)
{
    spc_dictionary_set_value(dictionary,key,(spc_value_t){.type=SPC_TYPE_DICT,.value.dict=object});
}

static spc_dictionary_t *session_display_configuration(const struct ios_scene_geometry *display)
{
    /* Real codecs in runs 39404428141/39405060101. Identity type 0 is the
     * primary display; its decoder normalizes displayID to 1. Mode preferred
     * scale is uint64, while refreshRate is an XPC double. Missing pixelSize
     * and bounds are derived by the real decoder from currentMode.size. */
    enum { FBS_DISPLAY_MAIN=0 };
    spc_dictionary_t *identity=session_encoded_object("FBSDisplayIdentity");
    spc_dictionary_set_int64(identity,"type",FBS_DISPLAY_MAIN);
    spc_dictionary_set_uint64(identity,"displayID",display->display_id);
    spc_dictionary_set_string(identity,"uniqueID","arlinux-wayland-1");
    spc_dictionary_t *mode=session_encoded_object("FBSDisplayMode");
    spc_dictionary_set_uint64(mode,"width",display->width);
    spc_dictionary_set_uint64(mode,"height",display->height);
    spc_dictionary_set_uint64(mode,"preferredScale",1);
    spc_dictionary_set_value(mode,"refreshRate",(spc_value_t){.type=SPC_TYPE_DOUBLE,.value.dbl=display->refresh/1000.0});
    spc_dictionary_t *configuration=session_encoded_object("FBSDisplayConfiguration");
    session_dictionary_object(configuration,"identity",identity);
    session_dictionary_object(configuration,"currentMode",mode);
    spc_dictionary_set_string(configuration,"name","ARLinux Wayland");
    spc_dictionary_set_string(configuration,"deviceName","ARLinux Wayland");
    spc_dictionary_set_string(configuration,"hardwareIdentifier","arlinux-wayland-1");
    return configuration;
}

static spc_dictionary_t *session_launch_transition(void)
{
    /* Run 39407602453: UIApplicationSceneTransitionContext reads NSNumber
     * object setting 7. The real lifecycle table routes action 1 to
     * activateEventsOnly:NO; 2 is the separate events-only activation. */
    enum { UI_CONTEXT_LIFECYCLE_ACTION=7, UI_LIFECYCLE_ACTIVATE=1 };
    spc_dictionary_t *context=session_encoded_object("UIApplicationSceneTransitionContext");
    spc_dictionary_t *settings=session_encoded_object("BSMutableSettings");
    spc_array_t *objects=spc_array_create(),*entry=spc_array_create();
    spc_array_set_value(entry,0,(spc_value_t){.type=SPC_TYPE_UINT64,.value.u64=UI_CONTEXT_LIFECYCLE_ACTION});
    spc_array_set_value(entry,1,(spc_value_t){.type=SPC_TYPE_INT64,.value.i64=UI_LIFECYCLE_ACTIVATE});
    spc_array_set_value(entry,2,(spc_value_t){.type=SPC_TYPE_BOOL,.value.u64=1});
    spc_array_set_value(objects,0,(spc_value_t){.type=SPC_TYPE_ARRAY,.value.array=entry});
    spc_dictionary_set_value(settings,"objects",(spc_value_t){.type=SPC_TYPE_ARRAY,.value.array=objects});
    session_dictionary_object(context,"otherSettings",settings);
    return context;
}

static void session_scene_frame(spc_array_t *objects,const struct ios_scene_geometry *display)
{
    /* The actual FBSSceneSettingsDiff decoder (run 39437411133) maps setting
     * 3 to NSValue.bs_CGRectValue -> setFrame:. BSSettings' false format is
     * keyed-archive data, not an XPC array or a BSXPCCoding dictionary.
     * Foundation's interoperable NSValue rectangle coding uses NS.special=3
     * and NS.rectval. CoreFoundation accepts XML CF$UID references as well
     * as binary-plist UIDs. See swift-corelibs-foundation NSGeometry.swift,
     * NSSpecialValue.swift and CFPropertyList.c. No archived app state is used.
     */
    enum { FBS_SCENE_FRAME=3, BS_SETTINGS_KEYED_ARCHIVE=0 };
    char archive[1600];
    int length=snprintf(archive,sizeof(archive),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<plist version=\"1.0\"><dict>"
        "<key>$archiver</key><string>NSKeyedArchiver</string>"
        "<key>$version</key><integer>100000</integer>"
        "<key>$top</key><dict><key>root</key><dict><key>CF$UID</key><integer>1</integer></dict></dict>"
        "<key>$objects</key><array><string>$null</string>"
        "<dict><key>$class</key><dict><key>CF$UID</key><integer>3</integer></dict>"
        "<key>NS.special</key><integer>3</integer>"
        "<key>NS.rectval</key><dict><key>CF$UID</key><integer>2</integer></dict></dict>"
        "<string>{{0, 0}, {%u, %u}}</string>"
        "<dict><key>$classname</key><string>NSValue</string>"
        "<key>$classes</key><array><string>NSValue</string><string>NSObject</string></array></dict>"
        "</array></dict></plist>",display->width,display->height);
    if (length<0 || (size_t)length>=sizeof(archive)) abort();
    unsigned char *data=malloc(length); if (!data) abort(); memcpy(data,archive,length);
    spc_array_t *frame=spc_array_create();
    spc_array_set_value(frame,0,(spc_value_t){.type=SPC_TYPE_UINT64,.value.u64=FBS_SCENE_FRAME});
    spc_array_set_value(frame,1,(spc_value_t){.type=SPC_TYPE_DATA,.value.data={.ptr=data,.size=length}});
    spc_array_set_value(frame,2,(spc_value_t){.type=SPC_TYPE_BOOL,.value.u64=BS_SETTINGS_KEYED_ARCHIVE});
    spc_array_set_value(objects,2,(spc_value_t){.type=SPC_TYPE_ARRAY,.value.array=frame});
}

spc_dictionary_t *ios_scene_create_event(const char *identifier,const struct ios_scene_geometry *display)
{
    enum { FBS_WORKSPACE_CREATE_SCENE=5, FBS_SCENE_DISPLAY_CONFIGURATION=2 };
    spc_dictionary_t *event=spc_dictionary_create(),*dictionary=spc_dictionary_create();
    spc_dictionary_set_string(event,"bs_encodedObjectClassName","FBSWorkspaceCreateSceneEvent");
    spc_dictionary_set_string(event,"sceneID",identifier);
    spc_dictionary_set_string(event,"specClass","UIApplicationSceneSpecification");
    /* The real codecs require an initial class setting, not just outer diff
     * metadata. Run 39400670557 shows BSSettingsEncodeAppendSettingArrayEntry:
     * objects is [[uint64 setting, value, bool format]], with true format
     * decoded by CoreFoundation's XPC bridge. */
    enum { BS_SETTINGS_CLASS=1, BS_SETTINGS_FOUNDATION_FORMAT=1 };
    const char *keys[]={"settings","clientSettings"};
    const char *diffs[]={"FBSSceneSettingsDiff","FBSSceneClientSettingsDiff"};
    const char *classes[]={"UIApplicationSceneSettings","UIApplicationSceneClientSettings"};
    for (unsigned int i=0;i<2;i++) {
        spc_dictionary_t *diff=spc_dictionary_create(),*changes=spc_dictionary_create();
        spc_array_t *objects=spc_array_create(),*entry=spc_array_create();
        spc_dictionary_set_string(diff,"bs_encodedObjectClassName",diffs[i]);
        spc_dictionary_set_string(diff,"settingsClass",classes[i]);
        spc_dictionary_set_string(changes,"bs_encodedObjectClassName","BSSettings");
        spc_array_set_value(entry,0,(spc_value_t){.type=SPC_TYPE_UINT64,.value.u64=BS_SETTINGS_CLASS});
        spc_array_set_value(entry,1,(spc_value_t){.type=SPC_TYPE_STRING,.value.str=strdup(classes[i])});
        spc_array_set_value(entry,2,(spc_value_t){.type=SPC_TYPE_BOOL,.value.u64=BS_SETTINGS_FOUNDATION_FORMAT});
        spc_array_set_value(objects,0,(spc_value_t){.type=SPC_TYPE_ARRAY,.value.array=entry});
        if (!i) {
            spc_array_t *configuration=spc_array_create();
            spc_array_set_value(configuration,0,(spc_value_t){.type=SPC_TYPE_UINT64,.value.u64=FBS_SCENE_DISPLAY_CONFIGURATION});
            spc_array_set_value(configuration,1,(spc_value_t){.type=SPC_TYPE_DICT,
                .value.dict=session_display_configuration(display)});
            /* No Foundation-format marker: BSSettings invokes BSXPCCoding
             * for this actual framework object rather than converting a dict. */
            spc_array_set_value(objects,1,(spc_value_t){.type=SPC_TYPE_ARRAY,.value.array=configuration});
            session_scene_frame(objects,display);
        }
        spc_dictionary_set_value(changes,"objects",(spc_value_t){.type=SPC_TYPE_ARRAY,.value.array=objects});
        spc_dictionary_set_value(diff,"changes",(spc_value_t){.type=SPC_TYPE_DICT,.value.dict=changes});
        spc_dictionary_set_value(event,keys[i],(spc_value_t){.type=SPC_TYPE_DICT,.value.dict=diff});
    }
    spc_dictionary_set_value(dictionary,"event",(spc_value_t){.type=SPC_TYPE_DICT,.value.dict=event});
    spc_dictionary_set_int64(dictionary,"message",FBS_WORKSPACE_CREATE_SCENE);
    session_dictionary_object(event,"transitionContext",session_launch_transition());
    return dictionary;
}
