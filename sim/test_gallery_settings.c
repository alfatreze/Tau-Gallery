/* Host test of fw2/gallery/gallery_settings.h. Compile with -DBUG=n for the mutants (1..5 must fail). */
#include <stdio.h>
#include <string.h>
#include "../fw2/gallery/gallery_settings.h"
static int fails;
static void chk(const char *w, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", w); if (!ok) fails++; }
int main(void)
{
    uint32_t w[GS_WORDS]; memset(w, 0, sizeof w);
    gs_vals_t d = gs_defaults();
    chk("defaults: resume on, picture 0, no crc", d.resume == 1u && d.last_idx == 0u && d.last_crc == 0u);
    gs_vals_t v = gs_unpack(w);
    chk("a word set of layout 0 gives the defaults, not its own bits", v.resume == 1u && v.last_idx == 0u);
    w[GS_W_LAYOUT] = 1u << 27; w[GS_W_RESUME] = 1u; w[GS_W_INDEX] = 7u; w[GS_W_CRC] = 0x1234u;
    v = gs_unpack(w);
    chk("layout 1 is read: resume off (bit 0 set), index and crc", v.resume == 0u && v.last_idx == 7u && v.last_crc == 0x1234u);
    w[GS_W_RESUME] = 0x4000005Eu; v = gs_unpack(w);
    chk("the player's default for this variable (bit 0 clear) means resume on", v.resume == 1u);
    w[GS_W_LAYOUT] = 2u << 27; v = gs_unpack(w);
    chk("another layout version gives the defaults", v.resume == 1u && v.last_idx == 0u);
    w[GS_W_LAYOUT] = 1u << 27; w[GS_W_RESUME] = 0xFFFFFFFEu; v = gs_unpack(w);
    chk("only bit 0 of word 2 is the resume flag", v.resume == 1u);
    w[GS_W_INDEX] = 0xFFFFFFFFu; w[GS_W_CRC] = 0xFFFFFFFFu; v = gs_unpack(w);
    chk("the 31-bit fields ignore bit 31", v.last_idx == GS_M31 && v.last_crc == GS_M31);

    memset(w, 0, sizeof w); w[1] = 0xC0DEC0DEu; w[GS_W_LAYOUT] = 0x80000123u; w[GS_W_RESUME] = 0xA0u; w[GS_W_CRC] = 0x80000000u; w[GS_W_INDEX] = 0x80000000u;
    gs_vals_t nv = { 0u, 0x7FFFFFFFu, 0x7FFFFFFFu };
    const uint32_t m = gs_pack(w, &nv);
    chk("pack: layout version set, bit 31 and the low bits of word 0 stay", w[GS_W_LAYOUT] == (0x80000123u | (1u << 27)));
    chk("pack: resume off sets bit 0 and leaves the other bits of word 2", w[GS_W_RESUME] == 0xA1u);
    chk("pack: the 31-bit fields are set and bit 31 stays", w[GS_W_CRC] == 0xFFFFFFFFu && w[GS_W_INDEX] == 0xFFFFFFFFu);
    chk("pack: word 1 (the Theme slice's) is never touched", w[1] == 0xC0DEC0DEu);
    chk("pack: the mask names exactly the words that changed", m == ((1u << GS_W_LAYOUT) | (1u << GS_W_RESUME) | (1u << GS_W_CRC) | (1u << GS_W_INDEX)));
    gs_vals_t r = gs_unpack(w);
    chk("pack then unpack round trip at the extremes", r.resume == 0u && r.last_idx == GS_M31 && r.last_crc == GS_M31);
    chk("packing the same values again changes nothing", gs_pack(w, &nv) == 0u);
    nv.last_idx = 5u; chk("changing only the index changes only word 6", gs_pack(w, &nv) == (1u << GS_W_INDEX));
    nv.resume = 1u; chk("turning resume on clears bit 0 again", gs_pack(w, &nv) == (1u << GS_W_RESUME) && (w[GS_W_RESUME] & 1u) == 0u && w[GS_W_RESUME] == 0xA0u);
    int uniq = sizeof gs_write_order == 4 && gs_write_order[3] == GS_W_LAYOUT;
    for (unsigned i = 0; i < 4; i++) for (unsigned j = i + 1; j < 4; j++) if (gs_write_order[i] == gs_write_order[j]) uniq = 0;
    chk("write order: the four words once each, word 0 last", uniq);

    gs_vals_t s = { 1u, 4u, 0x55u };
    chk("resume: the saved picture is chosen when index and crc fit", gs_resume_index(&s, 18u, 0x55u) == 4u);
    chk("resume: the crc is a 31-bit compare (bit 31 of the entry's crc is ignored)", gs_resume_index(&s, 18u, 0x80000055u) == 4u);
    chk("resume: another picture at that index gives 0", gs_resume_index(&s, 18u, 0x56u) == 0u);
    s.last_idx = 18u; chk("resume: an index equal to the count gives 0", gs_resume_index(&s, 18u, 0x55u) == 0u);
    s.last_idx = 17u; chk("resume: the last valid index is kept", gs_resume_index(&s, 18u, 0x55u) == 17u);
    s.last_idx = 4u; s.resume = 0u; chk("resume off gives 0", gs_resume_index(&s, 18u, 0x55u) == 0u);

    const uint32_t hz = 66666667u;
    chk("publish: not before a quiet second", !gs_publish_due(1000u + hz - 1u, 1000u, hz, 1));
    chk("publish: after a quiet second", gs_publish_due(1000u + hz, 1000u, hz, 1));
    chk("publish: half a second after a key is too early", !gs_publish_due(hz / 2u, 0u, hz, 1));
    chk("publish: nothing changed, nothing published", !gs_publish_due(10u * hz, 0u, hz, 0));
    chk("publish: the cycle counter wrapping is handled", gs_publish_due(hz, 0xFFFFFFFFu - 10u, hz, 1) && !gs_publish_due(100u, 0xFFFFFFFFu - 10u, hz, 1));
    chk("publish: wrap-around when the sum of last and one second does not overflow", gs_publish_due(0x10u, 0xFFFFFF00u, 100u, 1));
    printf(fails ? "FAILED %d\n" : "PASSED\n", fails);
    return fails != 0;
}
