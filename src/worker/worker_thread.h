#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <memory>
#include <uv.h>

#include "../message.h"
#include "../queue.h"
#include "../result.h"
#include "../status.h"
#include "../thread.h"

class WorkerPlatform;

class WorkerThread : public Thread
{
public:
  explicit WorkerThread(uv_async_t *main_callback);
  ~WorkerThread() override;

  void collect_status(Status &status) override;

  WorkerThread(const WorkerThread &) = delete;
  WorkerThread(WorkerThread &&) = delete;
  WorkerThread &operator=(const WorkerThread &) = delete;
  WorkerThread &operator=(WorkerThread &&) = delete;

private:
  Result<> wake() override;

  Result<> body() override;

  Result<CommandOutcome> handle_add_command(const CommandPayload *payload) override;

  Result<CommandOutcome> handle_remove_command(const CommandPayload *payload) override;

  std::unique_ptr<WorkerPlatform> platform;

  friend WorkerPlatform;
};

#endif
