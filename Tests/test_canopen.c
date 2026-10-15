/** @file test_canopen.c
 * @brief Native protocol, power-cut and application-to-bootloader handoff tests.
 * Dependency: portable Core modules only; no simulated successful STM32 update.
 */
#include "canopen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Test-only identities, not assigned production vendor/product identifiers. */
static const FiData factory = {1U, 42U, 7U, 1234U, 2U, 1U, 3U};

typedef struct {
    uint8_t pages[2][2048];
    int budget;
    bool read_error, erase_error, fail_commit_read;
} Memory;
static bool memRead(void *u, uint8_t p, uint32_t o, uint8_t *d, uint32_t n)
{
    Memory *m = u;
    if (m->read_error || (p > 1U) || (o + n > 2048U)) { return false; }
    if (m->fail_commit_read && n == 256U && Co_Get32(&m->pages[p][248]) == UINT32_C(0x434F4D54)) {
        m->fail_commit_read = false;
        return false;
    }
    memcpy(d, &m->pages[p][o], n); return true;
}
static bool memErase(void *u, uint8_t p)
{
    Memory *m = u;
    if (m->erase_error) { return false; }
    memset(m->pages[p], 255, 2048); return true;
}
static bool memWrite(void *u, uint8_t p, uint32_t o, const uint8_t *d, uint32_t n)
{
    Memory *m = u;
    uint32_t i;
    assert((o % 8U) == 0U && (n % 8U) == 0U);
    for (i = 0U; i < n; ++i) {
        if (m->budget == 0) { return false; }
        if (m->budget > 0) { --m->budget; }
        assert((m->pages[p][o+i] & d[i]) == d[i]);
        m->pages[p][o+i] &= d[i];
    }
    return true;
}
static void memoryInit(Memory *m, PsStore *s)
{
    PsIo io = {m, memRead, memErase, memWrite};
    memset(m, 0, sizeof(*m)); memset(m->pages, 255, sizeof(m->pages)); m->budget = -1;
    Ps_Init(s, &io);
}
static PsData identity(void)
{
    PsData d = {0};
    strcpy(d.device_name, "VEGA CANopen LOP gateway");
    strcpy(d.hardware_version, "A1"); strcpy(d.software_version, "2.0");
    d.product_code = 42U; d.revision = 1U; d.serial = 1234U;
    d.heartbeat_ms = 1000U; d.pending_slot = UINT32_MAX;
    return d;
}
static void commissioned(Memory *m, PsStore *s, PsData *d)
{
    PsData loaded;
    memoryInit(m, s); *d = identity();
    assert(Ps_Load(s, &loaded) == PS_EMPTY);
    assert(Ps_Save(s, d) == PS_OK);
}
static void testFactory(void)
{
    uint8_t record[FI_RECORD_SIZE], corrupt[FI_RECORD_SIZE], data[OD_BUFFER_SIZE];
    FiData decoded, invalid;
    FiCompatibility image = {1U, 42U, 2U, UINT32_C(0x00010003), UINT32_C(0x00010003)};
    ObjectDictionary od;
    PsData old = identity();
    uint32_t size, i, bit;
    char text[FI_TEXT_SIZE];
    assert(Fi_Encode(&factory, record, sizeof(record)) == FI_OK);
    assert(Co_Get32(&record[0]) == UINT32_C(0x46494431));
    assert(Co_Get32(&record[8]) == 128U && Co_Get32(&record[24]) == 1234U);
    assert(Co_Get32(&record[120]) == Ps_Crc(record, 120U));
    assert(Fi_Decode(record, sizeof(record), &decoded) == FI_OK);
    assert(decoded.serial == factory.serial && decoded.hardware_minor == 3U);
    assert(Fi_HardwareText(&decoded, text, sizeof(text)) && strcmp(text, "1.3") == 0);
    assert(!Fi_HardwareText(&decoded, text, sizeof(text) - 1U));
    for (i = 0U; i < FI_RECORD_SIZE; ++i) {
        assert(Fi_Decode(record, i, &decoded) == FI_INVALID && decoded.serial == 0U);
        memset(corrupt, 255, sizeof(corrupt)); memcpy(corrupt, record, i);
        assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) != FI_OK);
        for (bit = 0U; bit < 8U; ++bit) {
            memcpy(corrupt, record, sizeof(corrupt));
            corrupt[i] ^= (uint8_t)(1U << bit);
            assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) == FI_CORRUPT);
            assert(decoded.serial == 0U);
        }
    }
    memset(corrupt, 255, sizeof(corrupt));
    assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) == FI_EMPTY);
    memcpy(corrupt, record, sizeof(corrupt)); Co_Put32(&corrupt[4], 2U);
    Co_Put32(&corrupt[120], Ps_Crc(corrupt, 120U));
    assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) == FI_CORRUPT);
    memcpy(corrupt, record, sizeof(corrupt)); corrupt[36] = 0U;
    Co_Put32(&corrupt[120], Ps_Crc(corrupt, 120U));
    assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) == FI_CORRUPT);
    invalid = factory; invalid.serial = 0U;
    assert(Fi_Encode(&invalid, corrupt, sizeof(corrupt)) == FI_INVALID);
    memcpy(corrupt, record, sizeof(corrupt)); Co_Put32(&corrupt[24], 0U);
    Co_Put32(&corrupt[120], Ps_Crc(corrupt, 120U));
    assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) == FI_CORRUPT && decoded.serial == 0U);
    memcpy(corrupt, record, sizeof(corrupt)); Co_Put16(&corrupt[32], UINT16_MAX);
    Co_Put32(&corrupt[120], Ps_Crc(corrupt, 120U));
    assert(Fi_Decode(corrupt, sizeof(corrupt), &decoded) == FI_CORRUPT);
    assert(Fi_Compatible(&factory, &image));
    image.hardware_min++; assert(!Fi_Compatible(&factory, &image));
    image.hardware_min--; image.hardware_max--; assert(!Fi_Compatible(&factory, &image));
    image.hardware_max++; image.hardware_variant++; assert(!Fi_Compatible(&factory, &image));
    image.hardware_variant--; image.product_code++; assert(!Fi_Compatible(&factory, &image));
    image.product_code--; image.vendor_id++; assert(!Fi_Compatible(&factory, &image));
    image.vendor_id--; assert(!Fi_Compatible(&invalid, &image));
    assert(!Fi_Compatible(NULL, &image) && !Fi_Compatible(&factory, NULL));
    assert(Fi_ApplicationRange(FI_APPLICATION_START, 506U * 1024U));
    assert(Fi_ApplicationRange(FI_FLASH_ADDRESS - FI_PAGE_SIZE, FI_PAGE_SIZE));
    assert(!Fi_ApplicationRange(FI_APPLICATION_START, 506U * 1024U + 1U));
    assert(!Fi_ApplicationRange(FI_FLASH_ADDRESS, 1U));
    assert(!Fi_ApplicationRange(FI_FLASH_ADDRESS - 1U, 2U));
    assert(!Fi_ApplicationRange(FI_NVM_ADDRESS, FI_PAGE_SIZE));
    assert(!Fi_ApplicationRange(FI_APPLICATION_START - 1U, 1U));
    assert(!Fi_ApplicationRange(FI_APPLICATION_START, UINT32_MAX));
    assert(!Fi_ApplicationRange(FI_APPLICATION_START, 0U));
    invalid = factory; invalid.hardware_major = 65534U; invalid.hardware_minor = 65534U;
    assert(Fi_HardwareText(&invalid, text, sizeof(text)) && strcmp(text, "65534.65534") == 0);

    /* Conflicting legacy NVM fields must never override running/factory identity. */
    strcpy(old.device_name, "stale"); strcpy(old.software_version, "99.0");
    old.active_version = UINT32_MAX; old.serial = 999U;
    Od_Init(&od, &old, NULL, &factory, NULL, 1U);
    assert(Od_Read(&od, 0x1008U, 0U, data, &size) == 0U);
    assert(size == strlen(APP_MODULE_NAME) && memcmp(data, APP_MODULE_NAME, size) == 0);
    assert(Od_Read(&od, 0x100AU, 0U, data, &size) == 0U);
    assert(size == strlen(APP_SW_VERSION_TEXT) && memcmp(data, APP_SW_VERSION_TEXT, size) == 0);
    assert(Od_Read(&od, 0x2006U, 0U, data, &size) == 0U && Co_Get32(data) == APP_SW_VERSION_NUMBER);
    assert(Od_Read(&od, 0x1009U, 0U, data, &size) == 0U && size == 3U);
    assert(memcmp(data, "1.3", 3U) == 0);
    assert(Od_Read(&od, 0x1018U, 4U, data, &size) == 0U && Co_Get32(data) == factory.serial);
    assert(Od_Read(&od, 0x1018U, 3U, data, &size) == 0U && Co_Get32(data) == 7U);
    {
        static const uint16_t ro[] = {0x1008U, 0x1009U, 0x100AU, 0x2006U, 0x1018U};
        for (i = 0U; i < sizeof(ro) / sizeof(ro[0]); ++i) {
            uint8_t sub = (ro[i] == 0x1018U) ? 4U : 0U;
            assert(Od_CheckDownload(&od, ro[i], sub, 4U, true) == CO_ABORT_READ_ONLY);
            assert(Od_CheckDownload(&od, ro[i], sub, 0U, false) == CO_ABORT_READ_ONLY);
            assert(Od_Write(&od, ro[i], sub, data, 4U, 0U) == CO_ABORT_READ_ONLY);
        }
    }
    Od_Init(&od, &old, NULL, NULL, NULL, 1U);
    assert(!od.factory_valid);
    assert(Od_Read(&od, 0x1009U, 0U, data, &size) == 0U && size == 12U);
    assert(memcmp(data, "unconfigured", size) == 0);
    assert(Od_Read(&od, 0x1018U, 4U, data, &size) == 0U && Co_Get32(data) == 0U);
    data[0] = 1U;
    assert(Od_Write(&od, 0x2000U, 0U, data, 1U, 0U) == CO_ABORT_HARDWARE);
    puts("factory: ABI, 1024 bit corruptions, interrupted provisioning, compatibility, immutable OD OK");
}
static void testStorage(void)
{
    Memory m, baseline;
    PsStore s, reader;
    PsData a = identity(), b, loaded;
    int cut;
    memoryInit(&m, &s);
    assert(Ps_Crc((const uint8_t *)"123456789", 9U) == UINT32_C(0xCBF43926));
    assert(Ps_Load(&s, &loaded) == PS_EMPTY); assert(Ps_Save(&s, &a) == PS_OK);
    baseline = m; b = a;
    b.boot_request = BOOT_REQUEST_UPDATE; b.boot_node_id = 1U;
    b.boot_bitrate = 250000U; b.boot_protocol = BOOT_HANDOFF_PROTOCOL;
    for (cut = 0; cut < 256; ++cut) {
        m = baseline; m.budget = cut;
        Ps_Init(&s, &(PsIo){&m, memRead, memErase, memWrite});
        assert(Ps_Load(&s, &loaded) == PS_OK);
        assert(Ps_Save(&s, &b) == PS_IO_ERROR);
        Ps_Init(&reader, &s.io); assert(Ps_Load(&reader, &loaded) == PS_OK);
        assert(loaded.boot_request == BOOT_REQUEST_NONE && loaded.serial == a.serial);
    }
    m = baseline;
    Ps_Init(&s, &(PsIo){&m, memRead, memErase, memWrite});
    assert(Ps_Load(&s, &loaded) == PS_OK); assert(Ps_Save(&s, &b) == PS_OK);
    Ps_Init(&reader, &s.io); assert(Ps_Load(&reader, &loaded) == PS_OK);
    assert(loaded.boot_request == BOOT_REQUEST_UPDATE && loaded.boot_bitrate == 250000U);
    m.pages[reader.active_page][112] ^= 1U;
    assert(Ps_Load(&reader, &loaded) == PS_OK && loaded.boot_request == BOOT_REQUEST_NONE);
    b.boot_node_id = 128U; assert(Ps_Save(&s, &b) == PS_INVALID);
    b = a; memset(b.device_name, 'X', sizeof(b.device_name));
    assert(Ps_Save(&s, &b) == PS_INVALID);
    /* Load schema 1 with reserved FF bytes, migrate without inventing a request. */
    m = baseline;
    Co_Put32(&m.pages[0][4], 1U); memset(&m.pages[0][164], 255, 16U);
    Co_Put32(&m.pages[0][240], Ps_Crc(m.pages[0], 240U));
    Ps_Init(&s, &(PsIo){&m, memRead, memErase, memWrite});
    assert(Ps_Load(&s, &loaded) == PS_OK && loaded.boot_request == BOOT_REQUEST_NONE);
    assert(loaded.serial == 1234U); assert(Ps_Save(&s, &loaded) == PS_OK);
    assert(Co_Get32(&m.pages[s.active_page][4]) == 2U);
    m.read_error = true; assert(Ps_Load(&s, &loaded) == PS_IO_ERROR);
    assert(Ps_Save(&s, &a) == PS_INVALID);
    puts("storage: schema1->2 migration, CRC, 256 request-write power cuts, fallback OK");
}

typedef struct {
    PsStore *store;
    bool installed, deny_service, service, cancel_error, reset_saw_request;
    bool fail_commit_on_entry;
    Memory *memory;
    BootTxStatus tx;
    uint32_t reset_count, cancel_count;
} Board;
static bool installed(void *u) { return ((Board *)u)->installed; }
static bool enterService(void *u)
{
    Board *b = u;
    if (b->deny_service) { return false; }
    b->service = true;
    if (b->fail_commit_on_entry) { b->memory->fail_commit_read = true; }
    return true;
}
static void leaveService(void *u)
{
    Board *b = u;
    PsData latest;
    assert(Ps_Load(b->store, &latest) == PS_OK);
    assert(latest.boot_request == BOOT_REQUEST_NONE);
    b->service = false;
}
static BootTxStatus txStatus(void *u) { return ((Board *)u)->tx; }
static bool cancelAck(void *u)
{
    Board *b = u; b->cancel_count++;
    return !b->cancel_error;
}
static void resetBoard(void *u)
{
    Board *b = u;
    PsData d;
    assert(b->service && b->tx == BOOT_TX_CONFIRMED);
    assert(Ps_Load(b->store, &d) == PS_OK);
    b->reset_saw_request = d.boot_request == BOOT_REQUEST_UPDATE &&
        d.boot_node_id == 1U && d.boot_bitrate == 250000U && d.boot_protocol == 2U;
    b->reset_count++;
    /* Returning is intentionally a fault; real platform reset must not return. */
}
static BootHandoffPlatform boardInit(Board *b, PsStore *s, Memory *m)
{
    BootHandoffPlatform p = {0};
    memset(b, 0, sizeof(*b)); b->installed = true; b->store = s; b->memory = m;
    p.user = b; p.bitrate = 250000U; p.installed = installed;
    p.enterService = enterService; p.leaveService = leaveService;
    p.ackStatus = txStatus; p.cancelAck = cancelAck; p.reset = resetBoard;
    return p;
}
static void setupHandoff(BootHandoff *h, Board *b, PsStore *s, Memory *m, PsData *d)
{
    BootHandoffPlatform p;
    commissioned(m, s, d); p = boardInit(b, s, m);
    BootHandoff_Init(h, &p, s, d, 1U);
}
static void testHandoff(void)
{
    BootHandoff h;
    Board b;
    PsStore s;
    Memory m;
    PsData d, loaded;
    BootHandoffPlatform p;
    setupHandoff(&h, &b, &s, &m, &d);
    BootHandoff_Init(&h, NULL, &s, &d, 1U);
    assert(h.state == BOOT_UNAVAILABLE && BootHandoff_Request(&h, 0U) == CO_ABORT_STATE);
    assert(!b.service && b.reset_count == 0U);
    setupHandoff(&h, &b, &s, &m, &d);
    h.platform.cancelAck = NULL;
    assert(!BootHandoff_Available(&h) && BootHandoff_Request(&h, 0U) == CO_ABORT_STATE);
    setupHandoff(&h, &b, &s, &m, &d);
    memset(m.pages, 255, sizeof(m.pages));
    assert(BootHandoff_Request(&h, 0U) == CO_ABORT_HARDWARE);
    assert(h.result == BOOT_RESULT_STORAGE && !b.service && b.reset_count == 0U);
    setupHandoff(&h, &b, &s, &m, &d); b.installed = false;
    assert(BootHandoff_Request(&h, 0U) == CO_ABORT_STATE && !b.service);
    setupHandoff(&h, &b, &s, &m, &d); b.deny_service = true;
    assert(BootHandoff_Request(&h, 0U) == CO_ABORT_STATE);
    assert(h.result == BOOT_RESULT_SERVICE_DENIED && d.boot_request == 0U);
    setupHandoff(&h, &b, &s, &m, &d); m.erase_error = true;
    assert(BootHandoff_Request(&h, 0U) == CO_ABORT_HARDWARE);
    assert(h.result == BOOT_RESULT_STORAGE && !b.service && d.boot_request == 0U);
    setupHandoff(&h, &b, &s, &m, &d); b.fail_commit_on_entry = true;
    assert(BootHandoff_Request(&h, 0U) == CO_ABORT_HARDWARE);
    assert(h.result == BOOT_RESULT_STORAGE && !b.service);
    assert(Ps_Load(&s, &loaded) == PS_OK && loaded.boot_request == 0U);
    setupHandoff(&h, &b, &s, &m, &d);
    assert(BootHandoff_Request(&h, 0U) == 0U && b.service);
    assert(BootHandoff_Request(&h, 1U) == CO_ABORT_STATE);
    b.tx = BOOT_TX_CONFIRMED;
    BootHandoff_Poll(&h, 1U); assert(b.reset_count == 0U); /* Not queued yet. */
    BootHandoff_AckQueued(&h); BootHandoff_Poll(&h, 2U);
    assert(b.reset_count == 1U && b.reset_saw_request);
    assert(h.result == BOOT_RESULT_RESET_RETURNED && !b.service);
    assert(Ps_Load(&s, &loaded) == PS_OK && loaded.boot_request == 0U);
    setupHandoff(&h, &b, &s, &m, &d);
    assert(BootHandoff_Request(&h, UINT32_MAX - 3U) == 0U);
    BootHandoff_Poll(&h, 1995U); assert(h.state == BOOT_WAIT_ACK);
    BootHandoff_Poll(&h, 1996U);
    assert(h.result == BOOT_RESULT_TX_TIMEOUT && !b.service && b.reset_count == 0U);
    setupHandoff(&h, &b, &s, &m, &d);
    assert(BootHandoff_Request(&h, 0U) == 0U); BootHandoff_AckQueued(&h);
    b.tx = BOOT_TX_FAILED; BootHandoff_Poll(&h, 1U);
    assert(h.result == BOOT_RESULT_TX_FAILED && b.cancel_count == 1U && !b.service);
    setupHandoff(&h, &b, &s, &m, &d);
    assert(BootHandoff_Request(&h, 0U) == 0U); BootHandoff_AckQueued(&h);
    m.erase_error = true; BootHandoff_Poll(&h, 2000U);
    assert(h.result == BOOT_RESULT_RECOVERY_REQUIRED && b.service && BootHandoff_Busy(&h));
    assert(BootHandoff_Recover(&h) == CO_ABORT_HARDWARE && b.service);
    m.erase_error = false;
    assert(BootHandoff_Recover(&h) == 0U && !b.service && h.state == BOOT_IDLE);
    setupHandoff(&h, &b, &s, &m, &d);
    assert(BootHandoff_Request(&h, 0U) == 0U); BootHandoff_AckQueued(&h);
    b.cancel_error = true; BootHandoff_Poll(&h, 2000U);
    assert(h.result == BOOT_RESULT_RECOVERY_REQUIRED && b.service);
    b.cancel_error = false; assert(BootHandoff_Recover(&h) == 0U && !b.service);
    /* If application sees an unconsumed request after reset, it must NOT reset-loop. */
    setupHandoff(&h, &b, &s, &m, &d); assert(BootHandoff_Request(&h, 0U) == 0U);
    p = boardInit(&b, &s, &m); BootHandoff_Init(&h, &p, &s, &d, 1U);
    assert(h.state == BOOT_FAILED && h.result == BOOT_RESULT_RECOVERY_REQUIRED);
    BootHandoff_Poll(&h, 5000U); assert(b.reset_count == 0U);
    assert(BootHandoff_Recover(&h) == 0U && d.boot_request == 0U);
    puts("handoff: gating, service interlock, durable request, exact ACK/reset ordering, recovery OK");
}

static CoFrame request(uint8_t cmd, uint16_t index, uint8_t sub)
{
    CoFrame q = {0}; q.id = 0x601U; q.length = 8U; q.data[0] = cmd;
    Co_Put16(&q.data[1], index); q.data[3] = sub; return q;
}
static uint32_t abortCode(const CoFrame *r)
{
    assert(r->data[0] == 0x80U); return Co_Get32(&r->data[4]);
}
typedef struct { CoFrame last; uint32_t count; bool fail; } Sender;
static bool send(void *u, const CoFrame *f)
{
    Sender *s = u;
    if (s->fail) { return false; }
    s->last = *f; s->count++; return true;
}
static void testSdoAndCore(void)
{
    Memory memory;
    PsStore store;
    PsData d, loaded;
    Canopen c;
    Sender sender = {0};
    CoSdo *s;
    CoFrame q, r;
    uint8_t i;
    uint32_t rng = 1U, j;
    static const uint16_t removed[] = {0x2002U, 0x2003U, 0x2004U, 0x2005U, 0x2008U};
    static const uint8_t expected[4][8] = {
        {0x00,0x56,0x45,0x47,0x41,0x20,0x43,0x41},
        {0x10,0x4E,0x6F,0x70,0x65,0x6E,0x20,0x4C},
        {0x00,0x4F,0x50,0x20,0x67,0x61,0x74,0x65},
        {0x19,0x77,0x61,0x79,0x00,0x00,0x00,0x00}
    };
    commissioned(&memory, &store, &d);
    assert(!Co_Init(&c, 0U, send, &sender, &d, &store, &factory, NULL, 0U));
    assert(Co_Init(&c, 1U, send, &sender, &d, &store, &factory, NULL, 0U));
    Co_Poll(&c, 0U); assert(sender.last.id == 0x701U && sender.last.data[0] == 0U);
    Co_Poll(&c, 999U); assert(sender.count == 1U);
    Co_Poll(&c, 1000U); assert(sender.count == 2U && sender.last.data[0] == 5U);
    s = &c.sdo;
    q = request(0x40U, 0x1008U, 0U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && Co_Get32(&r.data[4]) == 24U);
    for (i = 0U; i < 4U; ++i) {
        q = request((uint8_t)(0x60U | ((i & 1U) << 4U)), 0U, 0U);
        assert(CoSdo_Receive(s, &q, i + 1U, &r));
        assert(memcmp(r.data, expected[i], 8U) == 0);
    }
    q = request(0x40U, 0x1018U, 4U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && Co_Get32(&r.data[4]) == 1234U);
    {
        static const uint16_t immutable[] = {0x1008U, 0x1009U, 0x100AU, 0x2006U, 0x1018U};
        for (i = 0U; i < sizeof(immutable) / sizeof(immutable[0]); ++i) {
            uint8_t sub = (immutable[i] == 0x1018U) ? 4U : 0U;
            q = request(0x23U, immutable[i], sub);
            assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_READ_ONLY);
            q = request(0x21U, immutable[i], sub); Co_Put32(&q.data[4], 32U);
            assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_READ_ONLY);
        }
        q = request(0x23U, 0x1018U, 4U); q.id = 0x201U;
        assert(!Co_Receive(&c, &q, 0U) && c.od.factory.serial == factory.serial);
    }
    q = request(0x40U, 0x1008U, 0U); assert(CoSdo_Receive(s, &q, 0U, &r));
    q = request(0x70U, 0U, 0U);
    assert(CoSdo_Receive(s, &q, 1U, &r) && abortCode(&r) == CO_ABORT_TOGGLE);
    q = request(0x40U, 0x1008U, 0U); assert(CoSdo_Receive(s, &q, UINT32_MAX - 3U, &r));
    assert(!CoSdo_Poll(s, 995U, &r));
    assert(CoSdo_Poll(s, 996U, &r) && abortCode(&r) == CO_ABORT_TIMEOUT);
    q = request(0x23U, 0x1000U, 0U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_READ_ONLY);
    q = request(0x40U, 0x1018U, 5U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_SUBINDEX);
    q = request(0x2BU, 0x1017U, 0U); Co_Put16(&q.data[4], 2345U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && r.data[0] == 0x60U);
    assert(Ps_Load(&store, &loaded) == PS_OK && loaded.heartbeat_ms == 2345U);
    memory.erase_error = true; Co_Put16(&q.data[4], 12U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_HARDWARE);
    assert(c.od.identity.heartbeat_ms == 2345U); memory.erase_error = false;
    for (i = 0U; i < sizeof(removed) / sizeof(removed[0]); ++i) {
        q = request(0x40U, removed[i], 0U);
        assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_NOT_FOUND);
        q = request(0x21U, removed[i], 0U); Co_Put32(&q.data[4], 4096U);
        assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_NOT_FOUND);
    }
    q = request(0x40U, 0x2009U, 0U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && Co_Get32(&r.data[4]) == 2U);
    q = request(0x40U, 0x200AU, 0U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && r.data[4] == 0U);
    q = request(0x40U, 0x200BU, 0U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && r.data[4] == 0U);
    q = request(0x2FU, 0x2000U, 0U); q.data[4] = 1U;
    assert(CoSdo_Receive(s, &q, 0U, &r) && abortCode(&r) == CO_ABORT_STATE);
    /* Atomic segmented scalar download; no side effect before the final segment. */
    q = request(0x21U, 0x1017U, 0U); Co_Put32(&q.data[4], 2U);
    assert(CoSdo_Receive(s, &q, 0U, &r) && r.data[0] == 0x60U);
    q = request(0x0BU, 0U, 0U); Co_Put16(&q.data[1], 1000U);
    assert(CoSdo_Receive(s, &q, 1U, &r) && r.data[0] == 0x20U);
    assert(c.od.identity.heartbeat_ms == 1000U);
    for (j = 0U; j < 65536U; ++j) {
        q.id = 0x601U;
        for (i = 0U; i < 8U; ++i) {
            rng = rng * UINT32_C(1664525) + UINT32_C(1013904223);
            q.data[i] = (uint8_t)(rng >> 24U);
        }
        q.length = (uint8_t)(rng & 15U);
        if (CoSdo_Receive(s, &q, j, &r)) { assert(r.id == 0x581U && r.length == 8U); }
        assert(s->offset <= OD_BUFFER_SIZE);
    }
    CoSdo_Reset(s);
    q = request(0x40U, 0x1001U, 0U);
    assert(Co_Receive(&c, &q, 1001U)); sender.fail = true;
    Co_Poll(&c, 1001U); assert(c.tx_pending && c.tx_failures == 1U);
    sender.fail = false; Co_Poll(&c, 1002U); assert(!c.tx_pending);
    q.id = 0x715U; q.length = 1U; q.data[0] = 5U;
    assert(!Co_Receive(&c, &q, 1003U) && CoHeartbeat_Valid(&q));
    q.length = 2U; assert(!CoHeartbeat_Valid(&q));
    assert(CoNmt_Command(&r, 1U, 21U) && r.id == 0U && r.data[1] == 21U);
    assert(Co_Error(&c, 0x8130U, 0x11U, 21U)); Co_Poll(&c, 1003U);
    assert(sender.last.id == 0x81U && c.od.error_count == 1U);
    puts("SDO/core: identity, exact frames, removed image OD, heartbeat, EMCY, 65536 malformed requests OK");
}

static void testEndToEnd(void)
{
    Memory memory;
    PsStore store;
    PsData d;
    Board b;
    BootHandoffPlatform p;
    Canopen c;
    Sender sender = {0};
    CoFrame q;
    commissioned(&memory, &store, &d); p = boardInit(&b, &store, &memory);
    assert(Co_Init(&c, 1U, send, &sender, &d, &store, &factory, &p, 0U)); Co_Poll(&c, 0U);
    q = request(0x2FU, 0x2000U, 0U); q.data[4] = 1U;
    assert(Co_Receive(&c, &q, 1U));
    assert(c.od.handoff.state == BOOT_WAIT_ACK && !c.od.handoff.ack_queued);
    assert(b.service && sender.count == 1U && b.reset_count == 0U);
    {
        uint8_t period[2] = {1U, 0U};
        assert(Od_Write(&c.od, 0x1017U, 0U, period, sizeof(period), 1U) == CO_ABORT_STATE);
        assert(c.od.identity.heartbeat_ms == 1000U);
    }
    sender.fail = true; Co_Poll(&c, 2U);
    assert(!c.od.handoff.ack_queued && b.reset_count == 0U);
    sender.fail = false; Co_Poll(&c, 3U);
    assert(c.od.handoff.ack_queued && sender.last.id == 0x581U && sender.last.data[0] == 0x60U);
    Co_Poll(&c, 4U); assert(b.reset_count == 0U);
    b.tx = BOOT_TX_CONFIRMED; Co_Poll(&c, 5U);
    assert(b.reset_count == 1U && b.reset_saw_request);
    /* Driver never accepted response: timeout replaces stale success with abort. */
    commissioned(&memory, &store, &d); p = boardInit(&b, &store, &memory);
    assert(Co_Init(&c, 1U, send, &sender, &d, &store, &factory, &p, 0U)); Co_Poll(&c, 0U);
    assert(Co_Receive(&c, &q, 1U)); sender.fail = true; Co_Poll(&c, 2U);
    Co_Poll(&c, 2001U); assert(c.pending.data[0] == 0x80U && !b.service && b.reset_count == 0U);
    sender.fail = false; Co_Poll(&c, 2002U);
    assert(sender.last.data[0] == 0x80U && d.boot_request == 0U);
    /* Segmented request: only final segment ACK can trigger reset. */
    commissioned(&memory, &store, &d); p = boardInit(&b, &store, &memory);
    assert(Co_Init(&c, 1U, send, &sender, &d, &store, &factory, &p, 0U)); Co_Poll(&c, 0U);
    q = request(0x21U, 0x2000U, 0U); Co_Put32(&q.data[4], 1U);
    assert(Co_Receive(&c, &q, 1U)); Co_Poll(&c, 1U);
    assert(!b.service && c.od.handoff.state == BOOT_IDLE && b.reset_count == 0U);
    q = request(0x0DU, 0U, 0U); q.data[1] = 1U;
    assert(Co_Receive(&c, &q, 2U)); Co_Poll(&c, 2U);
    assert(sender.last.data[0] == 0x20U && c.od.handoff.ack_queued);
    b.tx = BOOT_TX_CONFIRMED; Co_Poll(&c, 3U); assert(b.reset_count == 1U);
    /* Even an installed platform must not accept an unprovisioned device. */
    commissioned(&memory, &store, &d); p = boardInit(&b, &store, &memory);
    assert(Co_Init(&c, 1U, send, &sender, &d, &store, NULL, &p, 0U)); Co_Poll(&c, 0U);
    q = request(0x2FU, 0x2000U, 0U); q.data[4] = 1U;
    assert(Co_Receive(&c, &q, 1U)); Co_Poll(&c, 1U);
    assert(abortCode(&sender.last) == CO_ABORT_HARDWARE && !b.service && b.reset_count == 0U);
    assert(Ps_Load(&store, &d) == PS_OK && d.boot_request == BOOT_REQUEST_NONE);
    puts("integration: request -> durable journal -> exact expedited/segmented ACK -> reset OK");
}
int main(void)
{
    testFactory(); testStorage(); testHandoff(); testSdoAndCore(); testEndToEnd();
    puts("All application CANopen / bootloader handoff tests passed.");
    return 0;
}
