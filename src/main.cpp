#include <memory>
#include <iostream>
#include <string>
#include <sstream>
#include <functional>
#include <utility>
#include <unordered_map>
#include <vector>
#include <nan.h>
#include <v8.h>
#include <uv.h>

#include "log.h"
#include "queue.h"
#include "worker/worker_thread.h"

using v8::Local;
using v8::Value;
using v8::Object;
using v8::String;
using v8::Number;
using v8::Function;
using v8::FunctionTemplate;
using v8::Array;
using std::shared_ptr;
using std::unique_ptr;
using std::string;
using std::ostringstream;
using std::endl;
using std::unordered_map;
using std::vector;
using std::move;
using std::make_pair;

static void handle_events_helper(uv_async_t *handle);

class Main {
public:
  Main() : worker_thread{&event_handler}
  {
    int err;

    next_command_id = 0;
    next_channel_id = NULL_CHANNEL_ID + 1;

    err = uv_async_init(uv_default_loop(), &event_handler, handle_events_helper);
    if (err) return;

    worker_thread.run();
  }

  ChannelID send_worker_command(
    const CommandAction action,
    const std::string &&root,
    unique_ptr<Nan::Callback> callback,
    bool assign_channel_id = false
  )
  {
    CommandID command_id = next_command_id;
    ChannelID channel_id = NULL_CHANNEL_ID;

    if (assign_channel_id) {
      channel_id = next_channel_id;
      next_channel_id++;
    }

    CommandPayload command_payload(next_command_id, action, move(root), channel_id);
    Message command_message(move(command_payload));

    pending_callbacks.emplace(command_id, move(callback));

    next_command_id++;

    LOGGER << "Sending command " << command_message << " to worker thread." << endl;
    worker_thread.send(move(command_message));

    return channel_id;
  }

  void use_main_log_file(string &&main_log_file)
  {
    Logger::to_file(main_log_file.c_str());
  }

  void use_worker_log_file(string &&worker_log_file, unique_ptr<Nan::Callback> callback)
  {
    send_worker_command(COMMAND_LOG_FILE, move(worker_log_file), move(callback));
  }

  void watch(string &&root, unique_ptr<Nan::Callback> ack_callback, unique_ptr<Nan::Callback> event_callback)
  {
    string root_dup(root);
    ChannelID channel_id = send_worker_command(COMMAND_ADD, move(root), move(ack_callback), true);

    channel_callbacks.emplace(channel_id, move(event_callback));
  }

  void handle_events()
  {
    Nan::HandleScope scope;

    unique_ptr<vector<Message>> accepted = worker_thread.receive_all();
    if (!accepted) {
      return;
    }

    unordered_map<ChannelID, vector<Local<Object>>> to_deliver;

    for (auto it = accepted->begin(); it != accepted->end(); ++it) {
      const AckPayload *ack_message = it->as_ack();
      if (ack_message) {
        LOGGER << "Received ack message " << *it << "." << endl;

        auto maybe_callback = pending_callbacks.find(ack_message->get_key());
        if (maybe_callback == pending_callbacks.end()) {
          LOGGER << "Ignoring unexpected ack " << *it << "." << endl;
          continue;
        }

        unique_ptr<Nan::Callback> callback = move(maybe_callback->second);
        pending_callbacks.erase(maybe_callback);

        callback->Call(0, nullptr);
        continue;
      }

      const FileSystemPayload *filesystem_message = it->as_filesystem();
      if (filesystem_message) {
        LOGGER << "Received filesystem event message " << *it << "." << endl;

        ChannelID channel_id = filesystem_message->get_channel_id();

        Local<Object> js_event = Nan::New<Object>();
        js_event->Set(
          Nan::New<String>("actionType").ToLocalChecked(),
          Nan::New<Number>(static_cast<int>(filesystem_message->get_filesystem_action()))
        );
        js_event->Set(
          Nan::New<String>("entryKind").ToLocalChecked(),
          Nan::New<Number>(static_cast<int>(filesystem_message->get_entry_kind()))
        );
        js_event->Set(
          Nan::New<String>("oldPath").ToLocalChecked(),
          Nan::New<String>(filesystem_message->get_old_path()).ToLocalChecked()
        );
        js_event->Set(
          Nan::New<String>("newPath").ToLocalChecked(),
          Nan::New<String>(filesystem_message->get_new_path()).ToLocalChecked()
        );

        to_deliver[channel_id].push_back(js_event);
        continue;
      }

      LOGGER << "Received unexpected message " << *it << "." << endl;
    }

    for (auto it = to_deliver.begin(); it != to_deliver.end(); ++it) {
      ChannelID channel_id = it->first;
      vector<Local<Object>> js_events = it->second;

      auto maybe_callback = channel_callbacks.find(channel_id);
      if (maybe_callback == channel_callbacks.end()) {
        LOGGER << "Ignoring unexpected filesystem event channel " << channel_id << "." << endl;
        continue;
      }
      shared_ptr<Nan::Callback> callback = maybe_callback->second;

      LOGGER << "Dispatching " << js_events.size()
        << " event(s) on channel " << channel_id << " to node callbacks." << endl;

      Local<Array> js_array = Nan::New<Array>(js_events.size());

      int index = 0;
      for (auto et = js_events.begin(); et != js_events.end(); ++et) {
        js_array->Set(index, *et);
        index++;
      }

      Local<Value> argv[] = {
        Nan::Null(),
        js_array
      };
      callback->Call(2, argv);
    }
  }

private:
  uv_async_t event_handler;

  WorkerThread worker_thread;

  CommandID next_command_id;
  ChannelID next_channel_id;

  unordered_map<CommandID, unique_ptr<Nan::Callback>> pending_callbacks;
  unordered_map<ChannelID, shared_ptr<Nan::Callback>> channel_callbacks;
};

static Main instance;

static void handle_events_helper(uv_async_t *handle)
{
  instance.handle_events();
}

static bool get_string_option(Local<Object>& options, const char *key_name, string &out)
{
  Nan::HandleScope scope;
  const Local<String> key = Nan::New<String>(key_name).ToLocalChecked();

  Nan::MaybeLocal<Value> as_maybe_value = Nan::Get(options, key);
  if (as_maybe_value.IsEmpty()) {
    return true;
  }
  Local<Value> as_value = as_maybe_value.ToLocalChecked();
  if (as_value->IsUndefined()) {
    return true;
  }

  if (!as_value->IsString()) {
    ostringstream message;
    message << "configure() option " << key_name << " must be a String";
    Nan::ThrowError(message.str().c_str());
    return false;
  }

  Nan::Utf8String as_string(as_value);

  if (*as_string == nullptr) {
    ostringstream message;
    message << "configure() option " << key_name << " must be a valid UTF-8 String";
    Nan::ThrowError(message.str().c_str());
    return false;
  }

  out.assign(*as_string, as_string.length());
  return true;
}

void configure(const Nan::FunctionCallbackInfo<Value> &info)
{
  string main_log_file;
  string worker_log_file;
  bool async = false;

  Nan::MaybeLocal<Object> maybe_options = Nan::To<Object>(info[0]);
  if (maybe_options.IsEmpty()) {
    Nan::ThrowError("configure() requires an option object");
    return;
  }

  Local<Object> options = maybe_options.ToLocalChecked();
  if (!get_string_option(options, "mainLogFile", main_log_file)) return;
  if (!get_string_option(options, "workerLogFile", worker_log_file)) return;

  unique_ptr<Nan::Callback> callback(new Nan::Callback(info[1].As<Function>()));

  if (!main_log_file.empty()) {
    instance.use_main_log_file(move(main_log_file));
  }

  if (!worker_log_file.empty()) {
    instance.use_worker_log_file(move(worker_log_file), move(callback));
    async = true;
  }

  if (!async) {
    callback->Call(0, 0);
  }
}

void watch(const Nan::FunctionCallbackInfo<Value> &info)
{
  if (info.Length() != 3) {
    return Nan::ThrowError("watch() requires three arguments");
  }

  Nan::MaybeLocal<String> maybe_root = Nan::To<String>(info[0]);
  if (maybe_root.IsEmpty()) {
    Nan::ThrowError("watch() requires a string as argument one");
    return;
  }
  Local<String> root_v8_string = maybe_root.ToLocalChecked();
  Nan::Utf8String root_utf8(root_v8_string);
  if (*root_utf8 == nullptr) {
    Nan::ThrowError("watch() argument one must be a valid UTF-8 string");
    return;
  }
  string root_str(*root_utf8, root_utf8.length());

  unique_ptr<Nan::Callback> ack_callback(new Nan::Callback(info[1].As<Function>()));
  unique_ptr<Nan::Callback> event_callback(new Nan::Callback(info[2].As<Function>()));

  instance.watch(move(root_str), move(ack_callback), move(event_callback));
}

void unwatch(const Nan::FunctionCallbackInfo<Value> &info)
{
  if (info.Length() != 2) {
    return Nan::ThrowError("watch() requires two arguments");
  }

  unique_ptr<Nan::Callback> ack_callback(new Nan::Callback(info[1].As<Function>()));
  ack_callback->Call(0, nullptr);
}

void initialize(Local<Object> exports)
{
  exports->Set(
    Nan::New<String>("configure").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(configure)).ToLocalChecked()
  );
  exports->Set(
    Nan::New<String>("watch").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(watch)).ToLocalChecked()
  );
  exports->Set(
    Nan::New<String>("unwatch").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(unwatch)).ToLocalChecked()
  );
}

NODE_MODULE(sfw, initialize);
