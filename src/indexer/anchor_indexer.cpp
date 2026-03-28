#include <avengine/avengine_lib/string_slice.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/indexer/anchor_indexer.h>
#include <fcntl.h>
#include <unistd.h>

static int anchorLogFD = 0;

void openAnchorLog()
{
  anchorLogFD = open("ac_log.tsv", O_WRONLY | O_CREAT | O_APPEND, 0644);
}

void saveAnchorText(const avengine::string& targetUrl,
                    const avengine::vector<avengine::string_slice>& anchor)
{
  if (anchor.size() == 0 || anchorLogFD == 0)
  {
    return;
  }

  // Build the complete line into a single buffer so one write() call is issued.
  // A single write() to a regular file is serialized by the kernel's inode
  // lock, preventing lines from concurrent threads from being interleaved.
  avengine::vector<char> line;

  for (char i : targetUrl)
  {
    line.pushBack(i);
  }

  line.pushBack('\t');

  for (size_t i = 0; i < anchor.size(); i++)
  {
    if (i > 0)
    {
      line.pushBack(' ');
    }

    for (char j : anchor[i])
    {
      line.pushBack(j);
    }
  }
  line.pushBack('\n');

  write(anchorLogFD, line.begin(), line.size());
}

void closeAnchorLog()
{
  if (anchorLogFD > 0)
  {
    close(anchorLogFD);
    anchorLogFD = 0;
  }
}
