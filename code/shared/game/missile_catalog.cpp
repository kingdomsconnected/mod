// Missile compatibility and body materials from Data/Tables.pak.
#include "missile_catalog.h"

namespace KCDC::Shared::Game {
    const std::vector<MissileClassEntry> g_MissileClasses = {
        {{0x4B81A48D527027FFULL, 0x8FCA6E17D829BC9EULL}, 4, true, false, true}, // archery_bloodTrail_kafkaCrossbow
        {{0x4354E9A7278D26D1ULL, 0x452BB70CD237F984ULL}, 2, false, false, false}, // arrow_EXAMPLE
        {{0x414AB25CDFEA5D01ULL, 0x1821F8A51169B49AULL}, 2, false, false, false}, // arrow_crude
        {{0x441707A6C49AA63AULL, 0xD04C2A71F2979B9FULL}, 2, false, false, false}, // arrow_cutting
        {{0x483D11A213BA7468ULL, 0x28D2A236CE25B98CULL}, 2, false, false, false}, // arrow_enh_cutting
        {{0x47B5D620802507E9ULL, 0x6AE214C3FC0866AEULL}, 2, false, false, false}, // arrow_enh_hunting
        {{0x48311E11A5B31BBCULL, 0xDAA7135B6DC05B83ULL}, 2, false, false, false}, // arrow_enh_piercing
        {{0x4A47E3077DB6B854ULL, 0x9E46B290319439BAULL}, 2, false, false, false}, // arrow_enh_precise
        {{0x404B8974710E3706ULL, 0xEDF10E67516F3AB2ULL}, 2, false, false, false}, // arrow_hunting
        {{0x44D1AEC4AD6F0F01ULL, 0x4AB701EB10122C98ULL}, 2, false, false, false}, // arrow_normal
        {{0x46233714E5B3F681ULL, 0x974745FA1540BE97ULL}, 2, false, false, false}, // arrow_piercing
        {{0x44CB18BAD5E6764DULL, 0xA88577A14066D08DULL}, 2, false, false, false}, // arrow_precise
        {{0x48BC8D5CC20B2A42ULL, 0xA97C2029A5A424ADULL}, 2, false, false, false}, // arrow_tournament
        {{0x42BE696ED2A18521ULL, 0xBCD884AD820BF3ADULL}, 2, false, false, false}, // battle_arrow
        {{0x4DAC63CAEC38D11FULL, 0x9CB548F64FCEF5B9ULL}, 4, false, false, false}, // battle_bolt
        {{0x4ED72360FB30C64EULL, 0x4DFE24341B5305B8ULL}, 8, false, false, false}, // battle_shot
        {{0x4BAE8C80BCDD6887ULL, 0x6758A7747556A68AULL}, 4, false, false, false}, // bolt_crude
        {{0x4F35CBD87B0531BDULL, 0xD44F7E4672A826A6ULL}, 4, false, false, false}, // bolt_cutting
        {{0x42E94CB4E6652736ULL, 0x375F4064000512B0ULL}, 4, false, false, false}, // bolt_enh_cutting
        {{0x4D744AE1B738D184ULL, 0xD4B14319DBB8AC8FULL}, 4, false, false, false}, // bolt_enh_hunting
        {{0x42B73617C82F1A8DULL, 0x9412F76FE936A998ULL}, 4, false, false, false}, // bolt_enh_piercing
        {{0x449225E9081FC4A1ULL, 0x7AC068669D2DC88DULL}, 4, false, false, false}, // bolt_enh_precise
        {{0x42D8A63667C11981ULL, 0xEB250817F584B28BULL}, 4, false, false, false}, // bolt_fine
        {{0x4A60E96540337BEFULL, 0xA44F789A5E69EEABULL}, 4, false, false, false}, // bolt_hunting
        {{0x4713637F8460003FULL, 0x9C4B7C035449C992ULL}, 4, false, false, false}, // bolt_normal
        {{0x48DB5E17166B4C7BULL, 0xBFDCAC4EF2020DBFULL}, 4, false, false, false}, // bolt_piercing
        {{0x419080BD1ED55CB8ULL, 0x34D416C96D141B98ULL}, 4, false, false, false}, // bolt_precise
        {{0x4BC83472A431DA4EULL, 0x3B85BF7E35F51798ULL}, 4, false, false, false}, // bolt_tournament
        {{0x457033005C23394AULL, 0x479051111CEFB7A8ULL}, 2, true, true, false}, // bow_a
        {{0x4D95844F5E7BCA7BULL, 0xBEE8D737E51C22A1ULL}, 2, true, true, false}, // bow_a_empty
        {{0x454521830FFFB172ULL, 0x2FF33F4AE001DBBBULL}, 2, true, true, false}, // bow_b
        {{0x40A42CF9DB354284ULL, 0xAF0402028DE7FCBCULL}, 2, true, true, false}, // bow_b_a
        {{0x4B56C89041F61FE6ULL, 0xEC626B2F495459ABULL}, 2, true, true, false}, // bow_b_b
        {{0x44001012AB49C45DULL, 0x2FA710AA57BDAB90ULL}, 2, true, true, false}, // bow_b_b_empty
        {{0x4B21ED249F79CE5EULL, 0x40F24415927CAE97ULL}, 2, true, true, false}, // bow_b_empty
        {{0x43A12315D2011311ULL, 0x2943F02D437653A9ULL}, 2, true, true, false}, // bow_c
        {{0x47126C6CF54E6116ULL, 0x2B6E41E3118AA999ULL}, 2, true, true, false}, // bow_c_b
        {{0x4B7C6518C6367565ULL, 0x55207A0CFB2DA18CULL}, 2, true, true, false}, // bow_c_c
        {{0x4CCEBF92B4A0B9C9ULL, 0xB992C8570FF243ADULL}, 2, true, true, false}, // bow_d
        {{0x4A0EDAEE77262956ULL, 0xF48EF16975513590ULL}, 2, true, true, false}, // bow_d_a
        {{0x4DAC3E99AE873E17ULL, 0x7F889F4B483B0B83ULL}, 2, true, true, false}, // bow_e
        {{0x4C46BC3BCD1FE169ULL, 0x5EBE9F10D949D196ULL}, 2, true, true, false}, // bow_e_a
        {{0x4469BE33F03DD358ULL, 0xC88A1DF8D2B627A4ULL}, 4, true, false, true}, // combattest_crossbow_heavy
        {{0x43D3014D8C28B925ULL, 0xB612E349235EE295ULL}, 4, true, false, true}, // combattest_crossbow_heavy_bad
        {{0x498FB3F3CF767EDEULL, 0x01A70DF8884E66A6ULL}, 4, true, false, true}, // combattest_crossbow_light
        {{0x4D2B0EEEF9E192A7ULL, 0x21B4002D3EB2D3A6ULL}, 4, true, false, true}, // combattest_crossbow_light_bad
        {{0x4398DE2DC3C7B6C3ULL, 0x5BBD8F22A832B487ULL}, 4, true, false, true}, // combattest_crossbow_medium
        {{0x49D75D3ABE0B20C6ULL, 0xEF5384CE1A0F618DULL}, 4, true, false, true}, // combattest_crossbow_medium_bad
        {{0x4771D78CEAE7440BULL, 0xD16A914C44A57ABDULL}, 24, true, false, true}, // combattest_rifle_bad
        {{0x4348350785B8BF86ULL, 0x342336EE6C5C7483ULL}, 8, false, false, false}, // combattest_shot_ball
        {{0x4B3EF0FB588C12C6ULL, 0x739E73F21DCE7D84ULL}, 4, true, false, true}, // crossbowHeavyCheap01
        {{0x4C6A6EBDF0FB0494ULL, 0xD4C5B36D39EF9EBBULL}, 4, true, false, true}, // crossbowHeavyNormal01
        {{0x44F36A4254EC9F69ULL, 0xF3A8E9C26C8383AAULL}, 4, true, false, true}, // crossbowLightArtemis01
        {{0x4DDE05667673EFC2ULL, 0x2ECE90776CF9189EULL}, 4, true, false, true}, // crossbowLightCheap01
        {{0x45549CF2A6FC95EBULL, 0x77E1AA099C9A72ADULL}, 4, true, false, true}, // crossbowLightFancy01
        {{0x434C6EEECB6EE20BULL, 0xEC2B2E5031804CAFULL}, 4, true, false, true}, // crossbowLightNormal01
        {{0x47CA042BB77F912AULL, 0x6337ADBCDD5F428FULL}, 4, true, false, true}, // crossbowMediumCheap01
        {{0x48FF0CBB56287624ULL, 0xA8A3F14411D60ABFULL}, 4, true, false, true}, // crossbowMediumFancy01
        {{0x490EE78748F25A62ULL, 0xF93E30BF3593E983ULL}, 4, true, false, true}, // crossbowMediumNormal01
        {{0x4AE3B9BE0077DFA2ULL, 0xCBDF49BA032859AEULL}, 2, true, true, false}, // cuman_bow_a
        {{0x40988F49FEEA3905ULL, 0xA8452A575A7FB583ULL}, 2, true, true, false}, // cuman_bow_a_a
        {{0x47C775B6DDF6C62BULL, 0x5E817096263887B2ULL}, 2, true, true, false}, // cuman_bow_a_b
        {{0x403F91CD7B77A0E9ULL, 0x89E5C8BAE66B3EBEULL}, 2, true, true, false}, // cuman_bow_b
        {{0x46D4B371EA78735DULL, 0x8E08BBEBF0BE39A0ULL}, 24, true, false, true}, // handgonneNormal01
        {{0x4FB2BE402694BFEFULL, 0xECE3EDEA10E01B90ULL}, 24, true, false, true}, // handgunFancy01
        {{0x4E88D33B2BA148D1ULL, 0xFF322CE1C15C7C83ULL}, 4, true, false, true}, // havirskyTurnajMediumCrossbow
        {{0x4C2B54B8842C178AULL, 0x20531630D4775582ULL}, 24, true, false, true}, // hookgunFancy01
        {{0x4D057CA7D9CCF323ULL, 0x3EB28B743C21FBB8ULL}, 24, true, false, true}, // hookgunNormal01
        {{0x46D259D94ED3C017ULL, 0xEF24533654078C9CULL}, 24, true, false, true}, // hookgunPushek
        {{0x485E465855E0731FULL, 0xD0DC7EA8BD74CD96ULL}, 4, false, false, false}, // magickySip_magickaSipka
        {{0x4A64887494DE53E1ULL, 0x8B2EB68C75AE3798ULL}, 4, true, false, true}, // magickySip_pechCrossbow
        {{0x4310B1E2DBE25684ULL, 0x0D0CB69BDAE25681ULL}, 2, true, true, false}, // poi_skrysStarehoPytlaka_poachersBow
        {{0x4693FFC41ABE5629ULL, 0x86335EA70E70D79CULL}, 2, false, false, false}, // poi_strelnice_blackArrow
        {{0x4EADC082AD5BCF05ULL, 0xE7DDD3C6162F9CBEULL}, 4, false, false, false}, // posledniPomazani_bohutaOpBolt
        {{0x4702BA661C7C82D9ULL, 0x00621DDF18E08889ULL}, 2, true, true, false}, // prepadeni_banditBow
        {{0x4E0D352DE3D153B7ULL, 0x8963AAD7776598A9ULL}, 2, true, true, false}, // sedmStatecnych2_raneksBow
        {{0x40BFA41CF10DED12ULL, 0x5950844E3D88AEA8ULL}, 8, false, false, false}, // shot_ball
        {{0x4DC2D64CB302EE9DULL, 0xB48E237DCD0545B0ULL}, 16, false, false, false}, // shot_scatter
        {{0x436BD1DB398F4CBBULL, 0x8C31F1AFEEFBA3AEULL}, 2, false, false, false}, // test_arrow
        {{0x436BD1DB398F4CBBULL, 0x8E31F1AFEEFBA3AEULL}, 8, false, false, false}, // test_ball
        {{0x44DC734DE44EEAF7ULL, 0xE9C156AA5EEC558CULL}, 2, true, true, false}, // test_barbora_bow
        {{0x4818D46BB3B18AB1ULL, 0xB3E9E84291B92EB1ULL}, 2, false, false, false}, // test_barbora_coolArrow
        {{0x45EA92DC4D3AB56FULL, 0x76CA2DEF70A55D80ULL}, 2, true, true, false}, // test_barbora_coolBow
        {{0x4FF3AB7FE08B4EE1ULL, 0x8293CB1DB31E19B0ULL}, 2, false, false, false}, // test_barbora_lameArrow
        {{0x4718EA29B4DAD9F9ULL, 0xF3C6EA304462F387ULL}, 2, true, true, false}, // test_barbora_lameBow
        {{0x4A35011EA4D75B12ULL, 0xFEAF82601FDC1CA2ULL}, 2, false, false, false}, // test_barbora_sortofArrow
        {{0x436B0000398F4CBBULL, 0x8C31F1AFEEFBA3AEULL}, 2, false, false, false}, // test_better_arrow
        {{0x436BD1DB398F4CBBULL, 0x8D31F1AFEEFBA3AEULL}, 4, false, false, false}, // test_bolt
        {{0x4189803EE4F05E67ULL, 0xADECB04D93AD9EA0ULL}, 2, true, true, false}, // test_bow
        {{0x4FE9090A392878EBULL, 0xDBC51B0B03E5C9A1ULL}, 2, true, true, false}, // test_bow_brokable
        {{0x4189803EE4F05E67ULL, 0xDFECB04D93AD9EA0ULL}, 4, true, false, true}, // test_crossbow_heavy
        {{0x4189803EE4F05E67ULL, 0xAFECB04D93AD9EA0ULL}, 4, true, false, true}, // test_crossbow_light
        {{0x4189803EE4F05E67ULL, 0xBFECB04D93AD9EA0ULL}, 4, true, false, true}, // test_crossbow_medium
        {{0x462884DEEED20F9BULL, 0xBD3539E1AB280C92ULL}, 24, true, false, true}, // test_hookgun
        {{0x4189803EE4F05E67ULL, 0x00ECB04D93AD9EA0ULL}, 24, true, false, true}, // test_rifle
        {{0x436BD1DB398F4CBBULL, 0x8F31F1AFEEFBA3AEULL}, 16, false, false, false}, // test_shotgun
        {{0x47A486A971EB8E1DULL, 0x13E8762D4A9F758DULL}, 2, true, true, false}, // zachranaPtacka_caponRewardBow
        {{0x4E04D847808F9B56ULL, 0xFFC61240435A12BAULL}, 4, true, false, true}, // zbranePanaSemina_sukCrossbow
    };

    const std::vector<std::int32_t> g_MissileHumanBodyMaterials = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37};

    const std::vector<MissilePoisonEntry> g_MissilePoisons = {
        {{0x45DD212503845DA1ULL, 0x549E56BA7DCF15A3ULL}, 75}, // 03845da1-2125-45dd-a315-cf7dba569e54
        {{0x4A11C19A25BDBC39ULL, 0x6F8432C4166E0F8CULL}, 25}, // 25bdbc39-c19a-4a11-8c0f-6e16c432846f
        {{0x4AA4FF683A818F56ULL, 0x0337E7C33764C0BFULL}, 12}, // 3a818f56-ff68-4aa4-bfc0-6437c3e73703
        {{0x47C2A09A40B35ED9ULL, 0x2AA5409301A463B1ULL}, 100}, // 40b35ed9-a09a-47c2-b163-a4019340a52a
        {{0x4D4A080845275677ULL, 0x12E67AA78AE7ADBCULL}, 100}, // 45275677-0808-4d4a-bcad-e78aa77ae612
        {{0x4C006538743B8CA4ULL, 0xE8C85020AB290399ULL}, 8}, // 743b8ca4-6538-4c00-9903-29ab2050c8e8
        {{0x4133D4CFB1697366ULL, 0xE6F912F5C3973697ULL}, 50}, // b1697366-d4cf-4133-9736-97c3f512f9e6
        {{0x4803156CB56D79E6ULL, 0x6E92012FA873918DULL}, 4}, // b56d79e6-156c-4803-8d91-73a82f01926e
        {{0x4AFD4B7ED6079F34ULL, 0xC674466FE3EEE1AFULL}, 75}, // d6079f34-4b7e-4afd-afe1-eee36f4674c6
        {{0x40920E4ADD2612E4ULL, 0xC5CF5DF428877B86ULL}, 50}, // dd2612e4-0e4a-4092-867b-8728f45dcfc5
        {{0x4D04B177ED10DD29ULL, 0xB204FE76BE3C30A3ULL}, 16}, // ed10dd29-b177-4d04-a330-3cbe76fe04b2
        {{0x48225524F13D37C1ULL, 0xE4DDB0CCE1481595ULL}, 25}, // f13d37c1-5524-4822-9515-48e1ccb0dde4
    };
} // namespace KCDC::Shared::Game
