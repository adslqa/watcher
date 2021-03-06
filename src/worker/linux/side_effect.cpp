#include <string>
#include <utility>
#include <vector>

#include "../../message.h"
#include "../../message_buffer.h"
#include "../../result.h"
#include "side_effect.h"
#include "watch_registry.h"

using std::move;
using std::string;
using std::vector;

void SideEffect::track_subdirectory(string subdir, ChannelID channel_id)
{
  subdirectories.emplace_back(move(subdir), channel_id);
}

void SideEffect::enact_in(WatchRegistry *registry, MessageBuffer &messages)
{
  for (Subdirectory &subdir : subdirectories) {
    vector<string> poll_roots;
    Result<> r = registry->add(subdir.channel_id, subdir.path, true, poll_roots);
    if (r.is_error()) messages.error(subdir.channel_id, string(r.get_error()), false);

    for (string &poll_root : poll_roots) {
      messages.add(Message(CommandPayloadBuilder::add(subdir.channel_id, move(poll_root), true, 1).build()));
    }
  }
}
