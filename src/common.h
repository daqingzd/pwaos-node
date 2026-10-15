#ifndef CHROMIUM_PWAOS_THIRDPARTY_NODEJS_ADDON_COMMON_H_
#define CHROMIUM_PWAOS_THIRDPARTY_NODEJS_ADDON_COMMON_H_

#include <js_native_api.h>
#include <stdlib.h>  // abort()

// Empty value so that macros here are able to return NULL or void
#define NODE_API_RETVAL_NOTHING  // Intentionally blank #define

#define GET_AND_THROW_LAST_ERROR(env)                                    \
  do {                                                                   \
    const napi_extended_error_info *error_info;                          \
    napi_get_last_error_info((env), &error_info);                        \
    bool is_pending;                                                     \
    const char* err_message = error_info->error_message;                  \
    napi_is_exception_pending((env), &is_pending);                       \
    /* If an exception is already pending, don't rethrow it */           \
    if (!is_pending) {                                                   \
      const char* error_message = err_message != NULL ?                  \
                       err_message :                                     \
                      "empty error message";                             \
      napi_throw_error((env), NULL, error_message);                      \
    }                                                                    \
  } while (0)

// The basic version of GET_AND_THROW_LAST_ERROR. We cannot access any
// exceptions and we cannot fail by way of JS exception, so we abort.
#define FATALLY_FAIL_WITH_LAST_ERROR(env)                                      \
  do {                                                                         \
    const napi_extended_error_info* error_info;                                \
    napi_get_last_error_info((env), &error_info);                              \
    const char* err_message = error_info->error_message;                       \
    const char* error_message =                                                \
        err_message != NULL ? err_message : "empty error message";             \
    fprintf(stderr, "%s\n", error_message);                                    \
    abort();                                                                   \
  } while (0)

#define NODE_API_ASSERT_BASE(env, assertion, message, ret_val)           \
  do {                                                                   \
    if (!(assertion)) {                                                  \
      napi_throw_error(                                                  \
          (env),                                                         \
        NULL,                                                            \
          "assertion (" #assertion ") failed: " message);                \
      return ret_val;                                                    \
    }                                                                    \
  } while (0)

#define NODE_API_BASIC_ASSERT_BASE(assertion, message, ret_val)                \
  do {                                                                         \
    if (!(assertion)) {                                                        \
      fprintf(stderr, "assertion (" #assertion ") failed: " message);          \
      abort();                                                                 \
      return ret_val;                                                          \
    }                                                                          \
  } while (0)

// Returns NULL on failed assertion.
// This is meant to be used inside napi_callback methods.
#define NODE_API_ASSERT(env, assertion, message)                         \
  NODE_API_ASSERT_BASE(env, assertion, message, NULL)

// Returns empty on failed assertion.
// This is meant to be used inside functions with void return type.
#define NODE_API_ASSERT_RETURN_VOID(env, assertion, message)             \
  NODE_API_ASSERT_BASE(env, assertion, message, NODE_API_RETVAL_NOTHING)

#define NODE_API_BASIC_ASSERT_RETURN_VOID(assertion, message)                  \
  NODE_API_BASIC_ASSERT_BASE(assertion, message, NODE_API_RETVAL_NOTHING)

#define NODE_API_CALL_BASE(env, the_call, ret_val)                       \
  do {                                                                   \
    if ((the_call) != napi_ok) {                                         \
      GET_AND_THROW_LAST_ERROR((env));                                   \
      return ret_val;                                                    \
    }                                                                    \
  } while (0)

#define NODE_API_BASIC_CALL_BASE(env, the_call, ret_val)                       \
  do {                                                                         \
    if ((the_call) != napi_ok) {                                               \
      FATALLY_FAIL_WITH_LAST_ERROR((env));                                     \
      return ret_val;                                                          \
    }                                                                          \
  } while (0)

// Returns NULL if the_call doesn't return napi_ok.
#define NODE_API_CALL(env, the_call)                                     \
  NODE_API_CALL_BASE(env, the_call, NULL)

// Returns empty if the_call doesn't return napi_ok.
#define NODE_API_CALL_RETURN_VOID(env, the_call)                         \
  NODE_API_CALL_BASE(env, the_call, NODE_API_RETVAL_NOTHING)

#define NODE_API_BASIC_CALL_RETURN_VOID(env, the_call)                         \
  NODE_API_BASIC_CALL_BASE(env, the_call, NODE_API_RETVAL_NOTHING)

#define NODE_API_CHECK_STATUS(the_call)                                   \
  do {                                                                         \
    napi_status status = (the_call);                                           \
    if (status != napi_ok) {                                                   \
      return status;                                                           \
    }                                                                          \
  } while (0)

#define NODE_API_ASSERT_STATUS(env, assertion, message)                        \
  NODE_API_ASSERT_BASE(env, assertion, message, napi_generic_failure)

#define DECLARE_NODE_API_PROPERTY(name, func)                            \
  { (name), NULL, (func), NULL, NULL, NULL, napi_default, NULL }

#define DECLARE_NODE_API_GETTER(name, func)                              \
  { (name), NULL, NULL, (func), NULL, NULL, napi_default, NULL }

#define DECLARE_NODE_API_PROPERTY_VALUE(name, value)                     \
  { (name), NULL, NULL, NULL, NULL, (value), napi_default, NULL }

inline void DefineStaticInt32Property(napi_env env, napi_value target, const char* name, int32_t value) {
  napi_value js_value;
  napi_create_int32(env, value, &js_value);
  napi_property_descriptor desc = {
    name,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    js_value,
    (napi_property_attributes)(
        napi_static | napi_enumerable),
    nullptr
  };

  napi_define_properties(env, target, 1, &desc);
}

#include "runtime_client_api.h"
#include <node_api.h>
#include <ostream>
#include <functional>

extern napi_value GetOrCreateAppObj(napi_env env, PWAOS_APP_HANDLE app_handle);
extern napi_value GetOrCreateInstanceObj(napi_env env, PWAOS_INSTANCE_HANDLE instance_handle);
extern napi_value GetOrCreateInterfaceObj(napi_env env, PWAOS_OBJECT_INTERFACE_HANDLE interface_handle);
inline void throw_pwaos_error(napi_env env, pwaosResult result) {
  char msg[PWAOS_MAX_RESULT_NAME_SIZE];
  pwaos_resultToString(result, msg);
  napi_throw_error(env, nullptr, msg);
}

#define PWAOS_API_CALL(env, the_call) \
  do {                                                                   \
    pwaosResult result = the_call;                                       \
    if (result != PWAOS_RESULT_SUCCESS) {                                \
      throw_pwaos_error(env, result);                                    \
      return nullptr;                                                    \
    }                                                                    \
  } while (0)

#define PWAOS_API_CALL_AND_RETURN(env, the_call) \
  do {                                                                   \
    napi_value result_val;                                               \
    napi_create_int32(env, the_call, &result_val);                       \
    return result_val;                                                   \
  } while (0)

struct pwaosNodeResult {
  napi_value node_val;
  pwaosResult val;
  bool have_node_result;

  pwaosNodeResult(pwaosResult result)
    : val(result), have_node_result(false) {}
  pwaosNodeResult(pwaosResult result, napi_value node_result)
    : node_val(node_result), val(result), have_node_result(true) {}
};
struct pwaosPromiseData {
  napi_deferred deferred;
  pwaosNodeResult result;
  std::function<pwaosNodeResult()> work;

  pwaosPromiseData(napi_deferred d, std::function<pwaosNodeResult()> t)
      : deferred(d), result(PWAOS_RESULT_SUCCESS), work(std::move(t)) {}

  static void CompleteCallback(napi_env env, napi_value js_cb, void* context, void* data) {
    pwaosPromiseData* p = static_cast<pwaosPromiseData*>(context);
//    assert(p->result.have_node_result == false);

    if (p->result.val == PWAOS_RESULT_SUCCESS ||
        p->result.val == PWAOS_RESULT_PENDING) {
      napi_value result_val;
      napi_create_int32(env, p->result.val, &result_val);
      napi_resolve_deferred(env, p->deferred, result_val);
    } else {
      char msg[PWAOS_MAX_RESULT_NAME_SIZE];
      pwaos_resultToString(p->result.have_node_result ? p->result.val : PWAOS_RESULT_ERROR_INTERNAL, msg);
      napi_value error;
      napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &error);
      napi_reject_deferred(env, p->deferred, error);
    }

    delete p;
  }
};

#define PWAOS_API_CALL_AND_RETURN_PROMISE(env, task)                       \
  do { \
    napi_value promise;                                                    \
    napi_deferred deferred;                                                \
    NODE_API_CALL(env, napi_create_promise(env, &deferred, &promise));     \
                                                                           \
    auto data = std::make_unique<pwaosPromiseData>(deferred, task);         \
    auto execute = [](napi_env env, void* d) {                             \
      pwaosPromiseData* p = static_cast<pwaosPromiseData*>(d);             \
      p->result = p->work();                                               \
    };                                                                     \
    auto complete = [](napi_env env, napi_status status, void* d) {        \
      pwaosPromiseData* p = static_cast<pwaosPromiseData*>(d);             \
      if (p->result.val == PWAOS_RESULT_SUCCESS ||                         \
          p->result.val == PWAOS_RESULT_PENDING) {                   \
        napi_value result_val;                                             \
        if (!p->result.have_node_result) \
          napi_create_int32(env, p->result.val, &result_val);              \
        else \
          result_val = p->result.node_val;                                 \
        napi_resolve_deferred(env, p->deferred, result_val);               \
      } else {                                                             \
        char msg[PWAOS_MAX_RESULT_NAME_SIZE];                              \
        pwaos_resultToString(p->result.val, msg);                          \
        napi_value error;                                                  \
        napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &error);       \
        napi_reject_deferred(env, p->deferred, error);                     \
      }                                                                    \
      delete p;                                                            \
    };                                                                     \
                                                                           \
    napi_value work_name;                                                  \
    napi_create_string_utf8(env, #task, NAPI_AUTO_LENGTH, &work_name);     \
    napi_async_work work;                                                  \
    NODE_API_CALL(env, napi_create_async_work(env, nullptr, work_name,     \
                                              execute, complete, data.get(), &work)); \
    NODE_API_CALL(env, napi_queue_async_work(env, work));                  \
    data.release();                                                        \
    return promise;                                                        \
  } while (0)

struct pwaosPromiseData_tsfn : public pwaosPromiseData {
  napi_threadsafe_function tsfn;
  napi_value promise;
  void* data;

  pwaosPromiseData_tsfn(napi_deferred d,
                        std::function<pwaosNodeResult()> t,
                        napi_threadsafe_function f)
      : pwaosPromiseData(d, std::move(t)), tsfn(f), data(nullptr) {}
  ~pwaosPromiseData_tsfn() {
//    assert(data == nullptr);
    napi_release_threadsafe_function(tsfn, napi_tsfn_release);
  }
};
#define PWAOS_API_MAKE_TSFN_PROMISE(env, task, func_val, js_cb)                  \
  [&]() -> std::unique_ptr<pwaosPromiseData_tsfn> { \
    napi_value promise;                                                    \
    napi_deferred deferred;                                                \
    NODE_API_CALL(env, napi_create_promise(env, &deferred, &promise));     \
    auto data = std::make_unique<pwaosPromiseData_tsfn>(deferred, task, nullptr);       \
    if (!data) return nullptr;                                             \
    napi_value async_name;                                                 \
    napi_create_string_utf8(env, #js_cb, NAPI_AUTO_LENGTH, &async_name);   \
    napi_status status = napi_create_threadsafe_function(                  \
        env, func_val, nullptr, async_name, 0, 1, \
        nullptr, nullptr, data.get(), js_cb, &data->tsfn); \
    if (status != napi_ok) { \
      napi_throw_error(env, nullptr, "Failed to create threadsafe function"); \
      return nullptr; \
    } \
    data->promise = promise; \
    return data;                                                        \
  }()

#define PWAOS_API_CLEAR_CALLBACK(env, obj, method_name) \
  do { \
    napi_value _method; \
    napi_get_named_property((env), (obj), method_name, &_method); \
    napi_value js_null; \
    napi_get_null(env, &js_null); \
    napi_value _args[2] = { js_null, js_null }; \
    napi_call_function(env, obj, _method, 2, _args, nullptr); \
  } while (0)

#endif  // CHROMIUM_PWAOS_THIRDPARTY_NODEJS_ADDON_COMMON_H_
