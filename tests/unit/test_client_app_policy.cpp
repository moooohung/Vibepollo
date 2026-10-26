#include "../tests_common.h"
#include "src/client_app_policy.h"

namespace policy = client_app_policy;
namespace {
  constexpr auto guest = "11111111-1111-4111-8111-111111111111";
  constexpr auto owner = "22222222-2222-4222-8222-222222222222";
  policy::allowlist_t parse(const nlohmann::json &value) {
    policy::allowlist_t result;
    EXPECT_TRUE(policy::parse(value, result));
    return result;
  }
}

TEST(ClientAppPolicy, LegacyMissingOrNullIsUnrestricted) {
  const auto scope = parse(nullptr);
  EXPECT_FALSE(scope.has_value());
  EXPECT_TRUE(policy::allows(scope, owner));
  EXPECT_TRUE(policy::allows(scope, "", true));
  EXPECT_TRUE(policy::serialize(scope).is_null());
}

TEST(ClientAppPolicy, EmptyListDeniesAllIncludingSyntheticControls) {
  const auto scope = parse(nlohmann::json::array());
  ASSERT_TRUE(scope.has_value());
  EXPECT_FALSE(policy::allows(scope, guest));
  EXPECT_FALSE(policy::allows(scope, "", true));
  EXPECT_EQ(policy::serialize(scope), nlohmann::json::array());
}

TEST(ClientAppPolicy, SelectionUsesResolvedUuidNotNamesOrNumericAliases) {
  const auto scope = parse(nlohmann::json::array({guest}));
  EXPECT_TRUE(policy::allows(scope, guest));
  EXPECT_FALSE(policy::allows(scope, owner));
  EXPECT_FALSE(policy::allows(scope, "Guest Game Share"));
  EXPECT_FALSE(policy::allows(scope, "123456"));
  EXPECT_FALSE(policy::allows(scope, ""));
  EXPECT_FALSE(policy::allows(scope, guest, true));
}

TEST(ClientAppPolicy, CanonicalizesCaseAndDeduplicates) {
  const auto scope = parse(nlohmann::json::array({"AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"}));
  ASSERT_TRUE(scope);
  ASSERT_EQ(scope->size(), 1U);
  EXPECT_EQ(scope->front(), "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
  EXPECT_TRUE(policy::allows(scope, "AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"));
}

TEST(ClientAppPolicy, RejectsMalformedTypesWithoutExpandingAuthority) {
  const std::vector<nlohmann::json> invalid {
    true, 1, "null", guest, nlohmann::json::object(),
    nlohmann::json::array({guest, 1}), nlohmann::json::array({nullptr}),
    nlohmann::json::array({""}), nlohmann::json::array({"../Desktop"}),
    nlohmann::json::array({"11111111111141118111111111111111"}),
    nlohmann::json::array({"11111111-1111-4111-8111-11111111111z"})
  };
  for (const auto &value : invalid) {
    policy::allowlist_t scope = std::nullopt;
    EXPECT_FALSE(policy::parse(value, scope)) << value.dump();
    ASSERT_TRUE(scope);
    EXPECT_TRUE(scope->empty());
    EXPECT_FALSE(policy::allows(scope, guest));
  }
}

TEST(ClientAppPolicy, BoundsPolicySize) {
  nlohmann::json entries = nlohmann::json::array();
  for (int i = 0; i < 256; ++i) entries.push_back(guest);
  policy::allowlist_t scope;
  EXPECT_TRUE(policy::parse(entries, scope));
  entries.push_back(owner);
  EXPECT_FALSE(policy::parse(entries, scope));
  ASSERT_TRUE(scope);
  EXPECT_TRUE(scope->empty());
}

TEST(ClientAppPolicy, RoundTripPreservesUnrestrictedDeniedAndSelectedPolicies) {
  for (const auto &value : {nlohmann::json(nullptr), nlohmann::json::array(), nlohmann::json::array({guest, owner})}) {
    const auto original = parse(value);
    const auto document = nlohmann::json::parse(nlohmann::json({{"allowed_app_uuids", policy::serialize(original)}}).dump());
    EXPECT_EQ(parse(document.at("allowed_app_uuids")), original);
  }
}

TEST(ClientAppPolicy, UnknownOrDeletedUuidNeverFallsBackToAnotherProfile) {
  const auto scope = parse(nlohmann::json::array({guest}));
  EXPECT_FALSE(policy::allows(scope, owner));
  EXPECT_FALSE(policy::allows(scope, {}));
}

TEST(ClientAppPolicy, FailedPersistenceKeepsOnlyPreviouslyAndNewlyAllowedApps) {
  const auto previous = parse(nlohmann::json::array({guest, owner}));
  const auto requested = parse(nlohmann::json::array({guest}));
  EXPECT_EQ(policy::intersect(previous, requested), requested);
  EXPECT_EQ(policy::intersect(requested, previous), requested);
  EXPECT_EQ(policy::intersect(std::nullopt, requested), requested);
  EXPECT_EQ(policy::intersect(requested, std::nullopt), requested);
  EXPECT_FALSE(policy::intersect(std::nullopt, std::nullopt));
  const auto disjoint = policy::intersect(requested, parse(nlohmann::json::array({owner})));
  ASSERT_TRUE(disjoint);
  EXPECT_TRUE(disjoint->empty());
}

TEST(ClientAppPolicy, RetryPersistsAnAlreadyEffectiveRestrictionAfterWriteFailure) {
  policy::allowlist_t memory;
  policy::allowlist_t disk;
  const auto requested = parse(nlohmann::json::array({guest}));
  int attempts = 0;
  EXPECT_FALSE(policy::persist_update(memory, requested, [&] { ++attempts; return false; }));
  EXPECT_EQ(memory, requested);
  EXPECT_FALSE(disk);
  EXPECT_TRUE(policy::persist_update(memory, requested, [&] { ++attempts; disk = memory; return true; }));
  EXPECT_EQ(attempts, 2);
  EXPECT_EQ(disk, requested);
}

TEST(ClientAppPolicy, FailedExpansionNeverBroadensEffectiveAuthority) {
  auto memory = parse(nlohmann::json::array({guest}));
  EXPECT_FALSE(policy::persist_update(memory, std::nullopt, [] { return false; }));
  ASSERT_TRUE(memory);
  EXPECT_TRUE(policy::allows(memory, guest));
  EXPECT_FALSE(policy::allows(memory, owner));
}
