/**
 * @file src/client_app_policy.h
 * @brief Certificate-bound application access policy, independent of client-supplied IDs.
 */
#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace client_app_policy {
  // Missing/null preserves existing clients. An empty list explicitly denies all apps.
  using allowlist_t = std::optional<std::vector<std::string>>;

  inline std::string canonical_uuid(std::string_view uuid) {
    if (uuid.size() != 36) return {};
    std::string result;
    result.reserve(uuid.size());
    for (std::size_t i = 0; i < uuid.size(); ++i) {
      char c = uuid[i];
      if (i == 8 || i == 13 || i == 18 || i == 23) {
        if (c != '-') return {};
      } else {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return {};
      }
      result.push_back(c);
    }
    return result;
  }

  inline bool parse(const nlohmann::json &value, allowlist_t &output) {
    // Invalid durable policy must never silently become unrestricted.
    output = std::vector<std::string> {};
    if (value.is_null()) {
      output.reset();
      return true;
    }
    if (!value.is_array() || value.size() > 256) return false;
    std::vector<std::string> parsed;
    for (const auto &entry : value) {
      if (!entry.is_string()) return false;
      auto uuid = canonical_uuid(entry.get_ref<const std::string &>());
      if (uuid.empty()) return false;
      if (std::find(parsed.begin(), parsed.end(), uuid) == parsed.end()) parsed.push_back(std::move(uuid));
    }
    output = std::move(parsed);
    return true;
  }

  inline nlohmann::json serialize(const allowlist_t &policy) {
    return policy ? nlohmann::json(*policy) : nlohmann::json(nullptr);
  }

  inline bool allows(const allowlist_t &policy, std::string_view resolved_app_uuid, bool synthetic = false) {
    if (!policy) return true;
    if (synthetic) return false;
    const auto uuid = canonical_uuid(resolved_app_uuid);
    return !uuid.empty() && std::find(policy->begin(), policy->end(), uuid) != policy->end();
  }

  inline allowlist_t intersect(const allowlist_t &previous, const allowlist_t &requested) {
    if (!previous) return requested;
    if (!requested) return previous;
    std::vector<std::string> result;
    for (const auto &uuid : *previous) {
      if (allows(requested, uuid)) result.push_back(uuid);
    }
    return result;
  }

  template<class Persist>
  bool persist_update(allowlist_t &current, const allowlist_t &requested, Persist persist) {
    const auto previous = current;
    current = requested;
    // Equal in-memory values may still need writing after an earlier I/O error.
    if (persist()) return true;
    current = intersect(previous, requested);
    return false;
  }
}  // namespace client_app_policy
