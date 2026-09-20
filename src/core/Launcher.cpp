#include "core/Launcher.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

namespace {
// single-quote a path for the shell
std::string shq(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}
}  // namespace

int launchGame(const System& system, const Game& game,
               const LaunchOption& opt) {
  std::string script = joinPath(joinPath(emusDir(), system.id), opt.script);
  if (!fileExists(script)) {
    LOG_ERROR("launch script missing: %s", script.c_str());
    return 127;
  }
  std::string cmd = shq(script) + " " + shq(game.path);
  LOG_INFO("launch: %s", cmd.c_str());

  pid_t pid = fork();
  if (pid < 0) {
    LOG_ERROR("fork failed");
    return -1;
  }
  if (pid == 0) {
    // child: run the script in its own process group so signals behave
    setsid();
    execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    // retry on EINTR
  }
  int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  LOG_INFO("launch returned %d", code);
  return code;
}

}  // namespace ndsui
