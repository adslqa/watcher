#ifndef RENAME_BUFFER_H
#define RENAME_BUFFER_H

#include <memory>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>

#include "../../message.h"
#include "../../message_buffer.h"
#include "recent_file_cache.h"

// Forward declaration to be able to accept an Event argument
class Event;

// Filesystem entry that was flagged as participating in a rename by a received filesystem event.
class RenameBufferEntry
{
public:
  RenameBufferEntry(RenameBufferEntry &&original) noexcept;

  ~RenameBufferEntry() = default;

  RenameBufferEntry(const RenameBufferEntry &) = delete;
  RenameBufferEntry &operator=(const RenameBufferEntry &) = delete;
  RenameBufferEntry &operator=(RenameBufferEntry &&) = delete;

private:
  RenameBufferEntry(std::shared_ptr<PresentEntry> entry, bool current);

  std::shared_ptr<PresentEntry> entry;
  bool current;
  size_t age;

  friend class RenameBuffer;
};

class RenameBuffer
{
public:
  // Create an empty buffer.
  RenameBuffer() = default;

  ~RenameBuffer() = default;

  using Key = ino_t;

  // Observe a rename event for a filesystem event. Deduce the matching side of the rename, if possible,
  // based on the previous and currently observed state of the entry at that path. Return "true" if the event
  // is consumed, or "false" if it should be treated as something other than a rename.
  bool observe_event(Event &event, RecentFileCache &cache);

  // Enqueue creation and removal events for any buffer entries that have remained unpaired through two consecutive
  // event batches.
  //
  // Return the collection of unpaired Keys that were created during this run.
  std::shared_ptr<std::set<Key>> flush_unmatched(ChannelMessageBuffer &message_buffer);

  // Enqueue creation and removal events for buffer entries that map to any of the listed keys. Return the collection
  // of unpaired Keys that were aged, but not processed, during this run.
  std::shared_ptr<std::set<Key>> flush_unmatched(ChannelMessageBuffer &message_buffer,
    const std::shared_ptr<std::set<Key>> &keys);

  RenameBuffer(const RenameBuffer &) = delete;
  RenameBuffer(RenameBuffer &&) = delete;
  RenameBuffer &operator=(const RenameBuffer &) = delete;
  RenameBuffer &operator=(RenameBuffer &&) = delete;

private:
  bool observe_present_entry(ChannelMessageBuffer &message_buffer,
    RecentFileCache &cache,
    const std::shared_ptr<PresentEntry> &present,
    bool current);

  bool observe_absent(ChannelMessageBuffer &message_buffer,
    RecentFileCache &cache,
    const std::shared_ptr<AbsentEntry> &absent);

  std::unordered_map<Key, RenameBufferEntry> observed_by_inode;
};

#endif
