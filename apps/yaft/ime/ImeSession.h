#pragma once

#include "Composer.h"
#include "ImeController.h"
#include "MozcClient.h"

namespace ime {

/// A composer + mozc client + controller wired together. The controller
/// keeps a pointer to the client, so a session is neither copied nor moved.
struct ImeSession {
  ImeSession(Composer composer,
             MozcClient::Options mozcOptions,
             ImeConfig config = {})
    : client(std::move(mozcOptions))
    , controller(std::move(composer), &client, config) {}

  ImeSession(const ImeSession&) = delete;
  ImeSession& operator=(const ImeSession&) = delete;

  MozcClient client;
  ImeController controller;
};

} // namespace ime
