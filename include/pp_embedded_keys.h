#pragma once
// ===========================================================================
// ProtoPirate — keys compiled into the firmware (ship-with-firmware keystore)
// ===========================================================================
//
// WHAT THIS IS
//   Keys listed here are built into the firmware and loaded at boot, so the
//   key-fob next-code feature works without the operator having to put a file on
//   the SD card. The SD file (/DATA/KEYS/keeloq_mfcodes.txt) and the app's
//   add-key screen still work and add on top of these.
//
// WHERE THESE CAME FROM
//   These are the KeeLoq/AUT64 manufacturer "learning" keys that are already
//   published in the clear on GitHub (the decrypted Flipper keeloq_mfcodes /
//   SubGhz-Toolkit dumps). Because they are already public, they are compiled in
//   here so the supported makes work out of the box. VAG Type-2 (XTEA) is handled
//   separately by TEA_KEY in PPVag.h and needs no entry here.
//
// HOW TO ADD MORE KEYS (authorized testing only)
//   Add entries to PP_EMBEDDED_KEELOQ_KEYS below, then rebuild. Format: a name,
//   the 64-bit key, and the learning type.
//
//   learning is one of: PP_SIMPLE, PP_NORMAL, PP_MAGIC_XOR1, PP_MAGIC_SERIAL1,
//                       PP_MAGIC_SERIAL2, PP_MAGIC_SERIAL3, PP_UNKNOWN
//
// AUTHORIZED TESTING ONLY. Keys let the reader recover a fob's counter and build
// the next rolling code. Only use keys for fobs and vehicles you own or are
// permitted to test, and follow your local laws.

#include <cstdint>

// Learning-type shorthands (mirror keeloq::LearningType).
#define PP_UNKNOWN        0
#define PP_SIMPLE         1
#define PP_NORMAL         2
#define PP_MAGIC_XOR1     3
#define PP_MAGIC_SERIAL1  4
#define PP_MAGIC_SERIAL2  5
#define PP_MAGIC_SERIAL3  6

struct PpEmbeddedKey {
    const char* name;
    uint64_t    key;
    uint8_t     learning;
};

// --- KeeLoq-type manufacturer/learning keys built into the firmware ---------
// Sourced from the publicly-available decrypted keeloq_mfcodes (GitHub).
static const PpEmbeddedKey PP_EMBEDDED_KEELOQ_KEYS[] = {
    { "VAG_Key_1", 0x01376C86ADABCC43ull, PP_NORMAL },
    { "VAG_Key_2", 0x074DE859C12F36ABull, PP_NORMAL },
    { "VAG_Key_3", 0x02377C65CEDC42EAull, PP_NORMAL },
    { "VAG_Key_4", 0xA453E861D9B720FCull, PP_NORMAL },
    { "VAG_Key_5", 0x038AA37B1E561F83ull, PP_NORMAL },
    { "VAG_Key_6", 0x84B619C52E0A3FD7ull, PP_NORMAL },
    { "BenincaVA", 0x0D31C8E391691B29ull, PP_UNKNOWN },
    { "Rosh", 0x05AEDAABAA981903ull, PP_SIMPLE },
    { "Merlin", 0x3E461AB4F76DA19Bull, PP_NORMAL },
    { "Motorline", 0x572768229476CAFFull, PP_NORMAL },
    { "Pecinin", 0x352BABACA5F4DFE0ull, PP_SIMPLE },
    { "Rossi", 0xFADA12FADA34F0DAull, PP_SIMPLE },
    { "Steelmate", 0xB3E5625A8CCD7139ull, PP_NORMAL },
    { "Old_KIA_Hyu", 0x0000000000000000ull, PP_UNKNOWN },
    { "KIA_Hyundai_V3_4_New", 0xCB72B95709057920ull, PP_UNKNOWN },
    { "KIA_Hyundai_V5_New", 0x30D320F9CFEA15CBull, PP_UNKNOWN },
    { "DoorHan", 0x8455F43584941223ull, PP_SIMPLE },
    { "Alligator", 0xEEF3D4B2626C5578ull, PP_SIMPLE },
    { "Stilmatic", 0x0123456789ABCDEFull, PP_NORMAL },
    { "Mongoose", 0xD5A5E7B2A7C1A0BAull, PP_NORMAL },
    { "SL_A6-A9_Tomahawk_9010", 0x5504045708301203ull, PP_SIMPLE },
    { "Pantera", 0x4D41474E64796E65ull, PP_SIMPLE },
    { "SL_A2-A4", 0x9BF7F89BF8FE78DAull, PP_SIMPLE },
    { "Cenmax_St-5", 0xAAFBFBA8F7CFEDFCull, PP_SIMPLE },
    { "SL_B6_B9_dop", 0xF6E5D4B32ABADCFEull, PP_SIMPLE },
    { "Harpoon", 0x2587010764070864ull, PP_SIMPLE },
    { "Tomahawk_TZ-9030", 0x6408076407018725ull, PP_SIMPLE },
    { "Tomahawk_Z_X_3-5", 0x6F5E4D3B2AABCDEFull, PP_SIMPLE },
    { "Cenmax_St-7", 0x66B446B980AC752Bull, PP_SIMPLE },
    { "Sheriff", 0x12332594A9189478ull, PP_SIMPLE },
    { "Pantera_CLK", 0x4130850A82610A14ull, PP_SIMPLE },
    { "Cenmax", 0x1234567890123456ull, PP_SIMPLE },
    { "Alligator_S-275", 0x54365CB7676284F9ull, PP_SIMPLE },
    { "Guard_RF-311A", 0x1961DAC2EB198847ull, PP_NORMAL },
    { "Partisan_RX", 0xAD3C12A17028F11Eull, PP_SIMPLE },
    { "APS-1100_APS-2550", 0x68732C5056334728ull, PP_SIMPLE },
    { "Pantera_XS_Jaguar", 0x67732C5056334627ull, PP_SIMPLE },
    { "NICE_Smilo", 0x54524D534543494Eull, PP_SIMPLE },
    { "NICE_MHOUSE", 0x484D6C6D73545253ull, PP_SIMPLE },
    { "Dea_Mio", 0x08AEDAABAA981903ull, PP_SIMPLE },
    { "Genius_Bravo", 0xFAAC05600002FAACull, PP_NORMAL },
    { "FAAC_RC_XT", 0xFAAC05600001FAACull, PP_NORMAL },
    { "BFT", 0x4C6D4D7A55644F76ull, PP_UNKNOWN },
    { "Came_Space", 0x96638C36C99C2C9Bull, PP_SIMPLE },
    { "DTM_Neo", 0xFC4DE22CD5370320ull, PP_SIMPLE },
    { "GSN", 0xA9748532B7655297ull, PP_NORMAL },
    { "Beninca", 0xCEB6AE48B5C63ED2ull, PP_UNKNOWN },
    { "Elmes_Poland", 0x9C61FD3A47B8E25Cull, PP_NORMAL },
    { "IronLogic", 0x0102030410203040ull, PP_SIMPLE },
    { "IL-100_Smart", 0x4030201004030201ull, PP_SIMPLE },
    { "Comunello", 0xF1A3E552C8647E55ull, PP_NORMAL },
    { "Sommer_fsk476", 0x7BCBEED4376EDCBFull, PP_NORMAL },
    { "Normstahl", 0x9DA08153CF312BA7ull, PP_SIMPLE },
    { "KEY", 0x30317B307D794471ull, PP_SIMPLE },
    { "JCM_Tech", 0x89146E59537903B7ull, PP_SIMPLE },
    { "Novoferm", 0x314C3865304E3961ull, PP_SIMPLE },
    { "EcoStar", 0x8607427861394E30ull, PP_NORMAL },
    { "Gibidi", 0x2626991902991902ull, PP_SIMPLE },
    { "Aprimatic", 0x4772736565734769ull, PP_SIMPLE },
    { "Kingates_Stylo4k", 0x414C455831393830ull, PP_SIMPLE },
    { "Jolly_Motors", 0xBBEE9EDDAAF97ECCull, PP_SIMPLE },
    { "Centurion", 0x188B0544A9B1DF14ull, PP_NORMAL },
    { "Monarch", 0x4292903083134583ull, PP_NORMAL },
    { "KGB_Subaru", 0x0000000023304758ull, PP_UNKNOWN },
    { "Magic_2", 0x0000000033205748ull, PP_UNKNOWN },
    { "Magic_3", 0xAC35BB2759000000ull, PP_UNKNOWN },
    { "Magic_4", 0x9804655AA5000000ull, PP_UNKNOWN },
    { "Teco", 0xB51526E8F126D1D7ull, PP_UNKNOWN },
    { "Mutanco_Mutancode", 0xAA38A7A32189B5A1ull, PP_UNKNOWN },
    { "Leopard", 0x1067DC33E7D88A46ull, PP_UNKNOWN },
    { "Faraon", 0x8A326438B62287F5ull, PP_UNKNOWN },
    { "Reff", 0x6912FA557DC2418Aull, PP_UNKNOWN },
    { "ZX-730-750-1055", 0xB51A7AAB27351596ull, PP_UNKNOWN },
    { "Pandora_Test_Debug_2", 0x0132456789ABCDEFull, PP_SIMPLE },
    { "Pandora_PRO", 0x1B36977B281597ACull, PP_SIMPLE },
    { "Cardin_S449", 0x207DBBE59D386F44ull, PP_NORMAL },
    { "Pandora_DEA", 0x2156EB02D8E9B977ull, PP_SIMPLE },
    { "Pandora_GIBIDI", 0x2255EA01DBEABA76ull, PP_SIMPLE },
    { "Pandora_MCODE", 0x2354E900DAEBBB75ull, PP_SIMPLE },
    { "Pandora_Unknown_1", 0x2453EE07DDECBC72ull, PP_SIMPLE },
    { "Pandora_SUZUKI", 0x2552EF06DCEDBD73ull, PP_SIMPLE },
    { "Pandora_Unknown_2", 0x2651EC05DFEEBE70ull, PP_SIMPLE },
    { "Jarolift", 0x27193A9B117C0835ull, PP_UNKNOWN },
    { "Pandora_SUBARU", 0x3519B934A4227995ull, PP_SIMPLE },
    { "Pandora_M101", 0x381D7D9A2A17AE99ull, PP_SIMPLE },
    { "Pandora_PRO2", 0x7EAF1E9A392B19B9ull, PP_SIMPLE },
    { "Fiat_type", 0x52D6A31154536200ull, PP_NORMAL },
    { "Toyotexusa_l", 0x00AAE199948A8BB1ull, PP_UNKNOWN },
    { "Q", 0x00E0000000000000ull, PP_UNKNOWN },
    { "KIA_Hyundai_V3_4_Old", 0xA8F5DFFC8DAA5CDBull, PP_UNKNOWN },
    { "KIA_Hyundai_V5_Old", 0x535446524B453030ull, PP_UNKNOWN },
    { "KIAV6A", 0x638664A880AA23FCull, PP_UNKNOWN },
    { "KIAV6B", 0x6B8E6CA088A22BF4ull, PP_UNKNOWN },
    { "Beninca_ARC", 0x6EB6AE4815C63ED2ull, PP_UNKNOWN },
    { "FAAC_SLH", 0x53696C7669618C14ull, PP_UNKNOWN },
    { "HomeGate", 0xA36B4239C30F513Aull, PP_SIMPLE },
    { "FFFF_Simple", 0xFFFFFFFFFFFFFFFFull, PP_SIMPLE },
    { "Miserere", 0x6D69736572657265ull, PP_NORMAL },
    { "Superrollo", 0xAA8376E5F9174608ull, PP_NORMAL },
    { "Clemsa_Mutancode", 0xA1B58921A3A738AAull, PP_NORMAL },
    { "Genius", 0x53696C7669611804ull, PP_UNKNOWN },
    { "Erreka", 0x6982697565021997ull, PP_UNKNOWN },
    { "Pujol", 0xFFCC583618522154ull, PP_UNKNOWN },
    { "Wisniowski", 0xCDAB896745ABCDADull, PP_NORMAL },
    { "Doormatic", 0x28D8818108815987ull, PP_NORMAL },
    { "Elvox", 0xBD4D5939A5C2E9B6ull, PP_NORMAL },
    { "ET_Blue", 0x1961195819891992ull, PP_NORMAL },
    { "ET_Blue_Mix", 0x1679171218241925ull, PP_NORMAL },
    { "ATA_PTX4", 0x69D885C147ECE6B7ull, PP_NORMAL },
    { "Verex", 0x554D495F49543942ull, PP_NORMAL },
    { "Mc_Garcia", 0x6CF2248AD50B75EFull, PP_SIMPLE },
    { "Fadini", 0x37C6A58F497402B1ull, PP_SIMPLE },
    { "Pujol_Vario", 0x89146E1A537903B7ull, PP_SIMPLE },
    { "Seav", 0x472369229077CAFEull, PP_NORMAL },
    { "Wisniowski2", 0x3B0B8D6C7EA12B80ull, PP_NORMAL },
    { "Wisniowski1Rv", 0xADCDAB456789ABCDull, PP_NORMAL },
    { "AERF_Otros", 0x00D64C6900BBC55Bull, PP_UNKNOWN },
    { "AERF_Collbaix", 0x0AD64C69A0BBC55Bull, PP_UNKNOWN },
    { "AERF_Hy_dom", 0x0ED64C69E0BBC55Bull, PP_UNKNOWN },
    { "AERF_Medva", 0x10D64C6901BBC55Bull, PP_UNKNOWN },
    { "AERF_VDS", 0x12D64C6921BBC55Bull, PP_UNKNOWN },
    { "AERF_Temp", 0x15D64C6951BBC55Bull, PP_UNKNOWN },
    { "AERF_Sabutom", 0x17D64C6971BBC55Bull, PP_UNKNOWN },
    { "AERF_Motorgate", 0x4FD64C69F4BBC55Bull, PP_UNKNOWN },
    { "JCM1G_EMFA", 0x89146EF9537903B7ull, PP_UNKNOWN },
    { "JCM1G_DMIL", 0x89146E00537903B7ull, PP_UNKNOWN },
    { "JCM1G_Antonio_Meca", 0x89146E88537903B7ull, PP_UNKNOWN },
    { "JCM1G_Hy_dom", 0x89146E4B537903B7ull, PP_UNKNOWN },
    { "JCM1G_Baleato", 0x89146EC3537903B7ull, PP_UNKNOWN },
    { "JCM1G_Cas", 0x89146E78537903B7ull, PP_UNKNOWN },
    { "JCM1G_Cubells", 0x89146E41537903B7ull, PP_UNKNOWN },
    { "JCM1G_Cyacsa", 0x89146E11537903B7ull, PP_UNKNOWN },
    { "JCM1G_Forsa", 0x89146ED1537903B7ull, PP_UNKNOWN },
    { "JCM1G_Gandara", 0x89146EA0537903B7ull, PP_UNKNOWN },
    { "JCM1G_Pujol", 0x89146E53537903B7ull, PP_UNKNOWN },
    { "JCM1G_Gibidi", 0x89146E60537903B7ull, PP_UNKNOWN },
    { "JCM1G_Gibidi2", 0x89146E30537903B7ull, PP_UNKNOWN },
    { "JCM1G_Hybrid", 0x89146E71537903B7ull, PP_UNKNOWN },
    { "JCM1G_Tech", 0x89146E7B537903B7ull, PP_UNKNOWN },
    { "JCM1G_Norton", 0x89146E0A537903B7ull, PP_UNKNOWN },
    { "JCM1G_Nueva_Castilla", 0x89146EA9537903B7ull, PP_UNKNOWN },
    { "JCM1G_Puertas_Lorenzo", 0x89146EB8537903B7ull, PP_UNKNOWN },
    { "JCM1G_Serviparking", 0x89146E21537903B7ull, PP_UNKNOWN },
    { "JCM1G_Zibor", 0x89146E03537903B7ull, PP_UNKNOWN },
    { "JCM_Gen2", 0x012083E677191973ull, PP_UNKNOWN },
    { "Stagnoli", 0xE66BEA84516CE32Dull, PP_UNKNOWN },
    { "Telcoma", 0xAAEFADC47683768Cull, PP_UNKNOWN },
    { "Telcoma_tbl_lo", 0xCD23CA482367583Cull, PP_UNKNOWN },
    { "SEA", 0x1010101010101010ull, PP_SIMPLE },
    { "Unknown_7", 0x89336337B72386F4ull, PP_SIMPLE },
    { "Unknown_8", 0x6813FB547CC34289ull, PP_SIMPLE },
    { "Unknown_9", 0x1299334B2639CE6Cull, PP_SIMPLE },
    { "Unknown_11", 0x0C606F0C6F69EF4Dull, PP_SIMPLE },
    { "Unknown_12", 0x468AD8E732DB6411ull, PP_SIMPLE },
    { "Unknown_13", 0x3471042231910422ull, PP_SIMPLE },
    { "Unknown_21", 0x97C2294BBA403729ull, PP_SIMPLE },
    { "Unknown_22_Simple", 0x715272376457617Aull, PP_SIMPLE },
    { "Unknown_26_Simple", 0x1166DB32E8D98947ull, PP_SIMPLE },
    { "Unknown_27_Simple", 0x945B8F9EC345380Cull, PP_SIMPLE },
    { "Unknown_28", 0x1972100719800719ull, PP_SIMPLE },
    { "DoorHan_2", 0x8455F43584941233ull, PP_SIMPLE },
    { "Unknown_30", 0x77633C4046235637ull, PP_SIMPLE },
    { "Unknown_31", 0x1C707F1C7F79FE5Dull, PP_SIMPLE },
    { "Unknown_32", 0xE6F5C4A33AAACCEEull, PP_SIMPLE },
    { "Unknown_33", 0xE6F4C4A33AAACCEEull, PP_SIMPLE },
    { "Unknown_34", 0x02233584B9088468ull, PP_SIMPLE },
    { "Unknown_35", 0x87D2395BAA502739ull, PP_SIMPLE },
    { "Unknown_36", 0x539899F05EA841DBull, PP_SIMPLE },
    { "Unknown_37", 0xB7ABBABBBEB8BAB2ull, PP_SIMPLE },
    { "Unknown_38", 0xC1F94051960AAADFull, PP_SIMPLE },
    { "Unknown_39", 0xB4DCF40FCD460114ull, PP_SIMPLE },
    { "Unknown_40", 0x42A8507BA53F26FAull, PP_SIMPLE },
    { "Unknown_41", 0x635683FC9B6A67C5ull, PP_SIMPLE },
    { "Unknown_42", 0x7C5D973077E1CA60ull, PP_SIMPLE },
    { "Unknown_43", 0x3C1F4DAA969BB87Aull, PP_SIMPLE },
    { "Unknown_44", 0xEBDC8DC8EAF39FD6ull, PP_SIMPLE },
    { "Unknown_47", 0x94FABA8EB67684FCull, PP_SIMPLE },
    { "Unknown_53", 0x2750ED04DEEFBF71ull, PP_SIMPLE },
    { "Unknown_54", 0xAD3C12A17028F101ull, PP_SIMPLE },
    { "Unknown_55", 0x70732C505633492Aull, PP_SIMPLE },
    { "Unknown_56", 0xFA0EBCF967CB1257ull, PP_SIMPLE },
    { "Unknown_57", 0x2219017622190177ull, PP_SIMPLE },
    { "Unknown_58", 0x8070047111797064ull, PP_SIMPLE },
    { "Unknown_59", 0x1998100119730319ull, PP_NORMAL },
    { "Unknown_60", 0x0130682882644842ull, PP_NORMAL },
    { "Unknown_66", 0x55AA649B087757B2ull, PP_SIMPLE },
    { "Unknown_68", 0xF5E5D4B32ABADCFEull, PP_SIMPLE },
    { "Unknown_70", 0xF9E5D4B32ABADCFEull, PP_SIMPLE },
    { "Unknown_71", 0x74914BFC701C6622ull, PP_SIMPLE },
    { "Unknown_72", 0x9CD061A1D0F5AC3Eull, PP_NORMAL },

};
static const unsigned PP_EMBEDDED_KEELOQ_KEY_COUNT =
    sizeof(PP_EMBEDDED_KEELOQ_KEYS) / sizeof(PP_EMBEDDED_KEELOQ_KEYS[0]);
