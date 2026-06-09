#pragma once
#include <Arduino.h>
#include "MutatorBase.h"

// Max rhythm length (adjust if needed)
#define MAX_PATTERN_LEN 64

// Datel rhythm mutator: operates on a char pattern where 'x' = hit and
// '_' = rest. Shares DNA/chaos scaffolding with the rest of the swarm via
// MutatorBase; only the buffer operations and gene table are project-local.
class Mutator : public MutatorBase {
public:
  // gene set A..G -> 7 mutation operators
  Mutator(uint8_t id) : MutatorBase(id, "ABCDEFG", 7) {}

  void evolve(const char *input_pattern, const char *sender_dna,
              char *output_pattern /* must be >= MAX_PATTERN_LEN */) {
    uint8_t len = strlen(input_pattern);
    if (len > MAX_PATTERN_LEN) len = MAX_PATTERN_LEN;

    for (uint8_t i = 0; i < len; i++) buffer[i] = input_pattern[i];
    buf_len = len;

    // apply mutations driven by both DNAs (sender first, then own)
    mutate_by_dna(sender_dna);
    mutate_by_dna(get_dna());

    // final sanitize
    if (buf_len < 1) {
      buffer[0] = '_';
      buf_len = 1;
    }

    for (uint8_t i = 0; i < buf_len; i++) output_pattern[i] = buffer[i];
    output_pattern[buf_len] = '\0';

    bumpChaos();
  }

protected:
  void apply_mutation(char gene) override {
    switch (decode_gene(gene)) {
      case ADD_HIT: flip_random('_', 'x'); break;
      case REMOVE_HIT: flip_random('x', '_'); break;
      case BURST_RESTS: insert_rest_burst(); break;
      case DUPLICATE_SEGMENT: duplicate_seg(); break;
      case SHUFFLE_SEGMENT: shuffle_small(); break;
      case STRETCH: stretch(); break;
      case COMPRESS: compress(); break;
      default: break;
    }
  }

private:
  enum MutationType {
    ADD_HIT,
    REMOVE_HIT,
    BURST_RESTS,
    DUPLICATE_SEGMENT,
    SHUFFLE_SEGMENT,
    STRETCH,
    COMPRESS,
    NONE
  };

  static MutationType decode_gene(char g) {
    switch (g) {
      case 'A': return ADD_HIT;
      case 'B': return REMOVE_HIT;
      case 'C': return BURST_RESTS;
      case 'D': return DUPLICATE_SEGMENT;
      case 'E': return SHUFFLE_SEGMENT;
      case 'F': return STRETCH;
      case 'G': return COMPRESS;
      default: return NONE;
    }
  }

  void flip_random(char from, char to) {
    for (uint8_t tries = 0; tries < 4; tries++) {
      uint8_t i = random(buf_len);
      if (buffer[i] == from) {
        buffer[i] = to;
        return;
      }
    }
  }

  void insert_rest_burst() {
    if (buf_len >= MAX_PATTERN_LEN - 2) return;
    uint8_t pos = random(buf_len);
    for (int i = buf_len; i > pos; i--) buffer[i] = buffer[i - 1];
    buffer[pos] = '_';
    buf_len++;
  }

  void duplicate_seg() {
    if (buf_len >= MAX_PATTERN_LEN - 2) return;
    uint8_t start = random(buf_len);
    uint8_t length = 1 + random(3);
    for (uint8_t i = 0; i < length && buf_len < MAX_PATTERN_LEN; i++) {
      buffer[buf_len++] = buffer[(start + i) % buf_len];
    }
  }

  void shuffle_small() {
    if (buf_len < 2) return;
    uint8_t a = random(buf_len);
    uint8_t b = random(buf_len);
    char tmp = buffer[a];
    buffer[a] = buffer[b];
    buffer[b] = tmp;
  }

  void stretch() {
    if (buf_len >= MAX_PATTERN_LEN - 1) return;
    uint8_t pos = random(buf_len);
    for (int i = buf_len; i > pos; i--) buffer[i] = buffer[i - 1];
    buffer[pos] = '_';
    buf_len++;
  }

  void compress() {
    if (buf_len <= 1) return;
    uint8_t pos = random(buf_len);
    for (uint8_t i = pos; i < buf_len - 1; i++) buffer[i] = buffer[i + 1];
    buf_len--;
  }

  char buffer[MAX_PATTERN_LEN];
  uint8_t buf_len = 0;
};
