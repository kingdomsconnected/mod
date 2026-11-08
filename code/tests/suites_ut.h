#pragma once

// Suites written as one function that throws or returns non-zero on failure.
// Each runs whole inside a single case; the server-booting suites run in a
// process of their own through `KCDCTests --suite <name>`, because a server
// and its Node runtime come up once per process.
int RunAlchemySuite();
int RunSmithingSuite();
int RunNavigationSuite();
int RunNpcOrderSuite();
int RunStreamedAssetSuite();
int RunModItemTableSuite();
int RunCustomRecipeSuite();
int RunWorldInventorySuite();
int RunPatrolApiSuite();
int RunStashRpcSuite();
int RunWorldResourceSuite(bool emptyConfig);
#ifdef _WIN32
int RunHudElementsSuite();
int RunWorldBuilderSuite();
#endif

MODULE(alchemy_suite, { IT("passes the alchemy suite", { EQUALS(RunAlchemySuite(), 0); }); });
MODULE(smithing_suite, { IT("passes the smithing suite", { EQUALS(RunSmithingSuite(), 0); }); });
MODULE(navigation_suite, { IT("passes the navigation suite", { EQUALS(RunNavigationSuite(), 0); }); });
MODULE(npc_order_suite, { IT("passes the NPC order suite", { EQUALS(RunNpcOrderSuite(), 0); }); });
MODULE(streamed_asset_suite, { IT("passes the streamed asset suite", { EQUALS(RunStreamedAssetSuite(), 0); }); });
MODULE(mod_item_table_suite, { IT("passes the mod item table suite", { EQUALS(RunModItemTableSuite(), 0); }); });
MODULE(custom_recipe_suite, { IT("passes the custom recipe suite", { EQUALS(RunCustomRecipeSuite(), 0); }); });
MODULE(world_inventory_suite, { IT("passes the world inventory suite", { EQUALS(RunWorldInventorySuite(), 0); }); });
#ifdef _WIN32
MODULE(hud_elements_suite, { IT("passes the HUD elements suite", { EQUALS(RunHudElementsSuite(), 0); }); });
MODULE(world_builder_suite, { IT("passes the world builder suite", { EQUALS(RunWorldBuilderSuite(), 0); }); });
#endif
