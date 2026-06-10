#pragma once
#include <Arduino.h>
#include "MutatorBase.h"
#include "Pattern.h"

// Serialized-string output bound: each peck serializes to 7 chars, so the
// worst case is MAX_STEPS * 7 chars plus a NUL terminator.
#define MAX_PATTERN_LEN (MAX_STEPS * 7 + 1)

// Datel rhythm mutator: operates on a Step[] (rest / hit / peck). Shares
// DNA/chaos scaffolding with the rest of the swarm via MutatorBase; only the
// buffer operations and gene table are project-local. Genes A-G are the
// original rhythm operators (re-expressed on steps); H-N add peck/velocity
// operators.
class Mutator : public MutatorBase {
public:
  // gene set A..N -> 14 mutation operators
  Mutator(uint8_t id) : MutatorBase(id, "ABCDEFGHIJKLMN", 14) {}

  void evolve(const char *input_pattern, const char *sender_dna,
              char *output_pattern /* must be >= MAX_PATTERN_LEN */) {
    buf_len = parsePattern(String(input_pattern), buffer, MAX_STEPS);

    // apply mutations driven by both DNAs (sender first, then own)
    mutate_by_dna(sender_dna);
    mutate_by_dna(get_dna());

    sanitize();

    String s = serializePattern(buffer, buf_len);
    uint16_t n = s.length();
    if (n > MAX_PATTERN_LEN - 1) n = MAX_PATTERN_LEN - 1;
    for (uint16_t i = 0; i < n; i++) output_pattern[i] = s[i];
    output_pattern[n] = '\0';

    bumpChaos();
  }

protected:
  void apply_mutation(char gene) override {
    switch (decode_gene(gene)) {
      case ADD_HIT:           add_hit(); break;
      case REMOVE_HIT:        remove_hit(); break;
      case BURST_RESTS:       insert_rest(); break;
      case DUPLICATE_SEGMENT: duplicate_seg(); break;
      case SHUFFLE_SEGMENT:   shuffle_small(); break;
      case STRETCH:           insert_rest(); break;
      case COMPRESS:          compress(); break;
      case HIT_TO_PECK:       hit_to_peck(); break;
      case PECK_TO_HIT:       peck_to_hit(); break;
      case MUTATE_FREQ:       mutate_freq(); break;
      case MUTATE_CURVE:      mutate_curve(); break;
      case MUTATE_AMP:        mutate_amp(); break;
      case MUTATE_DUR:        mutate_dur(); break;
      case MUTATE_HIT_VEL:    mutate_hit_vel(); break;
      default: break;
    }
  }

private:
  enum MutationType {
    ADD_HIT,           // A
    REMOVE_HIT,        // B
    BURST_RESTS,       // C
    DUPLICATE_SEGMENT, // D
    SHUFFLE_SEGMENT,   // E
    STRETCH,           // F
    COMPRESS,          // G
    HIT_TO_PECK,       // H
    PECK_TO_HIT,       // I
    MUTATE_FREQ,       // J
    MUTATE_CURVE,      // K
    MUTATE_AMP,        // L
    MUTATE_DUR,        // M
    MUTATE_HIT_VEL,    // N
    NONE
  };

  static MutationType decode_gene(char g) {
    if (g >= 'A' && g <= 'N') return (MutationType)(g - 'A');
    return NONE;
  }

  // ---- helpers ----

  // Index of a uniformly-random step matching `t`, or -1 if there are none.
  int random_of_type(StepType t) {
    uint8_t matches[MAX_STEPS];
    uint8_t m = 0;
    for (uint8_t i = 0; i < buf_len; i++)
      if (buffer[i].type == t) matches[m++] = i;
    if (m == 0) return -1;
    return matches[random(m)];
  }

  static int signed_step(int magnitude) {
    return random(2) ? magnitude : -magnitude;
  }

  // ---- rhythm operators (A-G) ----

  void add_hit() {
    int i = random_of_type(STEP_REST);
    if (i < 0) return;
    buffer[i].type = STEP_HIT;
    buffer[i].velocity = HIT_AMP_DEFAULT;
  }

  void remove_hit() {
    int i = random_of_type(STEP_HIT);
    if (i < 0) return;
    buffer[i].type = STEP_REST;
  }

  void insert_rest() {
    if (buf_len >= MAX_STEPS) return;
    uint8_t pos = random(buf_len + 1);
    for (int i = buf_len; i > pos; i--) buffer[i] = buffer[i - 1];
    buffer[pos].type = STEP_REST;
    buf_len++;
  }

  void duplicate_seg() {
    if (buf_len == 0 || buf_len >= MAX_STEPS) return;
    uint8_t start = random(buf_len);
    uint8_t length = 1 + random(3);
    uint8_t src_len = buf_len; // freeze source extent
    for (uint8_t i = 0; i < length && buf_len < MAX_STEPS; i++)
      buffer[buf_len++] = buffer[(start + i) % src_len];
  }

  void shuffle_small() {
    if (buf_len < 2) return;
    uint8_t a = random(buf_len);
    uint8_t b = random(buf_len);
    Step tmp = buffer[a];
    buffer[a] = buffer[b];
    buffer[b] = tmp;
  }

  void compress() {
    if (buf_len <= 1) return;
    uint8_t pos = random(buf_len);
    for (uint8_t i = pos; i < buf_len - 1; i++) buffer[i] = buffer[i + 1];
    buf_len--;
  }

  // ---- peck / velocity operators (H-N) ----

  void hit_to_peck() {
    int i = random_of_type(STEP_HIT);
    if (i < 0) return;
    buffer[i] = makeDefaultPeck();
  }

  void peck_to_hit() {
    int i = random_of_type(STEP_PECK);
    if (i < 0) return;
    Step &st = buffer[i];
    uint8_t amp = st.amp;
    st.type = STEP_HIT;
    st.velocity = clampHitVel(amp);
  }

  void mutate_freq() {
    int i = random_of_type(STEP_PECK);
    if (i < 0) return;
    buffer[i].freq = clampFreq((int)buffer[i].freq + signed_step(1));
  }

  void mutate_curve() {
    int i = random_of_type(STEP_PECK);
    if (i < 0) return;
    buffer[i].curve = clampCurve((int)buffer[i].curve + signed_step(1));
  }

  void mutate_amp() {
    int i = random_of_type(STEP_PECK);
    if (i < 0) return;
    buffer[i].amp = clampPeckAmp((int)buffer[i].amp + signed_step(random(8, 25)));
  }

  void mutate_dur() {
    int i = random_of_type(STEP_PECK);
    if (i < 0) return;
    buffer[i].dur = clampDur((int)buffer[i].dur + signed_step(1));
  }

  void mutate_hit_vel() {
    int i = random_of_type(STEP_HIT);
    if (i < 0) return;
    buffer[i].velocity =
        clampHitVel((int)buffer[i].velocity + signed_step(random(8, 25)));
  }

  // ---- sanitize ----

  void sanitize() {
    if (buf_len == 0) {
      buffer[0].type = STEP_HIT;
      buffer[0].velocity = HIT_AMP_DEFAULT;
      buf_len = 1;
      return;
    }
    // ensure not entirely rests
    for (uint8_t i = 0; i < buf_len; i++)
      if (buffer[i].type != STEP_REST) return;
    uint8_t i = random(buf_len);
    buffer[i].type = STEP_HIT;
    buffer[i].velocity = HIT_AMP_DEFAULT;
  }

  Step buffer[MAX_STEPS];
  uint8_t buf_len = 0;
};
