/* Host-side launch configuration. A helper never becomes a GUI application
 * merely because its parent exported WAYLAND_DISPLAY. */
#pragma once
enum native_launch_role { NATIVE_CONSOLE, NATIVE_APPLICATION, NATIVE_SERVICE };
struct native_launch_options {
    const char *program,*dyld,*root;
    enum native_launch_role role;
};
static struct native_launch_options native_launch_parse(int argc,char **argv)
{
    struct native_launch_options options={0};
    int role_seen=0;
    for (int i=1;i<argc;i++) {
        const char *argument=argv[i];
        if (!strcmp(argument,"--dyld") || !strcmp(argument,"--root") || !strcmp(argument,"--role")) {
            need(i+1<argc,"launch option requires a value");
            const char *value=argv[++i];
            if (!strcmp(argument,"--dyld")) { need(!options.dyld,"duplicate --dyld"); options.dyld=value; }
            else if (!strcmp(argument,"--root")) { need(!options.root,"duplicate --root"); options.root=value; }
            else {
                need(!role_seen,"duplicate --role"); role_seen=1;
                if (!strcmp(value,"application")) options.role=NATIVE_APPLICATION;
                else if (!strcmp(value,"service")) options.role=NATIVE_SERVICE;
                else need(!strcmp(value,"console"),"unknown process role");
            }
        } else {
            need(argument[0]!='-' && !options.program,"unknown launch option or multiple programs");
            options.program=argument;
        }
    }
    need(options.program!=NULL,"missing ARM64 Mach-O program");
    need(!options.root || options.dyld,"--root requires --dyld");
    need(options.role==NATIVE_CONSOLE || options.root,"application/service requires an iOS root");
    return options;
}
