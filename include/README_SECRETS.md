# Loading the header "secrets.h" in Platformio

The "secrets.h" file conntains the set of password or sensitive data used to access your wifi network and network services (MQTT etc).

A template of the file is available as ```secrets_template.h```
Using the ED_WIFI library as a *GIT submodule* causes errors of linking of the "secrets.h" due to some inconsistency in the resolution of the include path by platformio at build time.

Errors like "sectrets.h not found" [even if the file is in place] can be solved with the following configuration
## secrets.h as a file in the local repository
1. secrets.h saved in some  folder under the root, at the desider level, such as ```src/certs``` *{notice the forward slash}*
2. in platformio.ini, add
```
[env:main_noBT]
build_flags=
    -I src/certs
```
**important** placing the same in the build_flags under ```[env]``` does *not* work

## secrets.h as a file in centralized storage location
Purpose being to avoid to have multiple copies of the secrets.h with potential misalignments and risk of exposing the contents in GIT repositories.

In this setup, the secrets.h file it's stored in a fixed folder mapped using a system environment variable.
let's assume the variable is named ```ESP_HEADERS```
1. create the system variable- the variable should store the path with the forward slash convention such as ```D:/MyStuff/Software/VSrepos/sharedIncludes```
2. in platformio ini add to build flags the statment:
```
[env:config]
build_flags= -D DEBUG_MODE
    -I${sysenv.ESP_HEADERS}
```
notice, as remarked above, that this *does not work* if placed in ```[env]```


# Certificate

the loading of the server certificate is described in the ED_MQTT library


