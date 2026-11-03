#include <catch2/catch_test_macros.hpp>

#include "TempFiles.h"
#include "rMLibTestHelper.h"

#include "Calculator.h"
#include "Dialog.h"

#include <UI/Navigator.h>

#include <cstdlib>

using namespace rmlib;
using namespace tilem;

TEST_CASE("Tilem", "[tilem][ui]") {
  TemporaryDirectory tmp(/* cwd */ true);
  const std::string romPath = "unit_test.rom";

  auto ctx = TestContext::make();

  ctx.pumpWidget(Center(Navigator(Calculator(romPath))));

  auto calc = ctx.findByType<Calculator>();

  REQUIRE_THAT(calc, ctx.matchesGolden("tilem-init.png"));

  ctx.pump();

  REQUIRE_THAT(calc, ctx.matchesGolden("tilem-error.png"));

  SECTION("Exit") {
    ctx.tap(ctx.findByText("Exit"));
    ctx.pump();
    REQUIRE(ctx.shouldStop());
  }

  SECTION("Download") {
    // Skip in sandboxes and when the ROM source is unreachable or rate
    // limited (wget follows redirects).
    const auto probe = std::string("wget --spider -q --tries=1 --timeout=5 '") +
                       DownloadDialog::romUrl + "' >/dev/null 2>&1";
    if (std::system(probe.c_str()) != 0) {
      SKIP("Skipping download test - ROM source not reachable");
    }

    ctx.tap(ctx.findByText("Download"));

    // Wait for the download to finish.
    ctx.pump();
    while (!ctx.findByText("Downloading ROM '" + romPath + "' ...").empty()) {
      ctx.pump();
    }
    ctx.pump(std::chrono::milliseconds(500));

    REQUIRE_THAT(calc, ctx.matchesGolden("tilem-start.png"));

    ctx.press(ctx.findByType<Keypad>(), { 0.01f, 0.99f });
    ctx.pump(std::chrono::milliseconds(500));
    ctx.release(ctx.findByType<Keypad>(), { 0.01f, 0.99f });

    ctx.pump(std::chrono::milliseconds(500));
    REQUIRE_THAT(calc, ctx.matchesGolden("tilem-on.png"));

    ctx.tap(ctx.findByText("X"));
    ctx.pump();
    REQUIRE(ctx.shouldStop());
  }
}

TEST_CASE("Tilem Landscape", "[tilem][ui]") {
  TemporaryDirectory tmp(/* cwd */ true);
  const std::string romPath = "unit_test.rom";

  auto ctx = TestContext::make(/*keyboardAttached=*/true);

  ctx.pumpWidget(Center(Navigator(Calculator(romPath))));

  auto calc = ctx.findByType<Calculator>();
  REQUIRE_THAT(calc, ctx.matchesGolden("tilem-landscape.png"));
}
