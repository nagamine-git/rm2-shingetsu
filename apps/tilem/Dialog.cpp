#include "Dialog.h"

using namespace rmlib;

void
DownloadDialog::State::init(AppContext& ctx, const BuildContext& buildCtx) {
  startTimer = ctx.addTimer(std::chrono::seconds(0), [&] {
    auto cmd = "wget -O '" + getWidget().romPath + "' " + romUrl;

    std::cout << cmd << "\n";
    system(cmd.c_str());

    Navigator::of(buildCtx).pop();
  });
}

DownloadDialog::State
DownloadDialog::createState() {
  return State{};
}
