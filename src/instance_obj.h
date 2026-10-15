#ifndef CHROMIUM_PWAOS_THIRD_PARTY_NODEJS_ADDON_INSTANCE_OBJ_H_
#define CHROMIUM_PWAOS_THIRD_PARTY_NODEJS_ADDON_INSTANCE_OBJ_H_

extern napi_value InitInstanceClass(napi_env env, napi_value exports);
extern napi_value InitAppClass(napi_env env, napi_value exports);
extern void ReleaseAllInstances(napi_env env);
extern void ReleaseAllApps(napi_env env);
extern void ReleaseAllInterfaces(napi_env env);

#endif // CHROMIUM_PWAOS_THIRD_PARTY_NODEJS_ADDON_INSTANCE_OBJ_H_
