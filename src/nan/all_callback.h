#ifndef ALL_CALLBACK_H
#define ALL_CALLBACK_H

#include <forward_list>
#include <functional>
#include <list>
#include <memory>
#include <nan.h>
#include <v8.h>

#include "functional_callback.h"

class AllCallback
{
public:
  static std::shared_ptr<AllCallback> create(std::unique_ptr<Nan::Callback> &&done);

  ~AllCallback() = default;
  AllCallback(const AllCallback &) = delete;
  AllCallback(AllCallback &&) = delete;

  std::unique_ptr<Nan::Callback> create_callback();

  void fire_if_empty();

  AllCallback &operator=(const AllCallback &) = delete;
  AllCallback &operator=(AllCallback &&) = delete;

private:
  explicit AllCallback(std::unique_ptr<Nan::Callback> &&done);

  void callback_complete(size_t callback_index, const Nan::FunctionCallbackInfo<v8::Value> &info);

  std::unique_ptr<Nan::Callback> done;
  bool fired;
  size_t total;
  size_t remaining;

  std::forward_list<FnCallback> functions;

  Nan::Persistent<v8::Value> error;
  Nan::Persistent<v8::Array> results;

  std::list<std::shared_ptr<AllCallback>>::iterator me;

  static std::list<std::shared_ptr<AllCallback>> retained;
};

#endif
