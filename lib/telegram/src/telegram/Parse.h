#pragma once

// Turns Telegram's JSON into the structs in Types.h. Used by TelegramClient;
// kept separate so it can be tested on its own.

#include <ArduinoJson.h>

#include "telegram/Types.h"

namespace fvb {
namespace telegram {

bool parseUser(JsonVariantConst json, User& user);
bool parseMessage(JsonVariantConst json, Message& message);
bool parseCallbackQuery(JsonVariantConst json, CallbackQuery& query);
// Returns false if the update has no update_id. An update of a type the box
// doesn't use still parses, as Update::Type::Other.
bool parseUpdate(JsonVariantConst json, Update& update);
bool parsePhotoSizes(JsonVariantConst json, std::vector<PhotoSize>& sizes);

// Adds a "reply_markup" with tap buttons to a request.
void writeKeyboard(JsonObject request, const InlineKeyboard& keyboard);

}  // namespace telegram
}  // namespace fvb
