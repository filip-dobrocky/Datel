#pragma once
#include <Arduino.h>

const char* MUTATOR_TAG = "Mutator";

// Max rhythm length (adjust if needed)
#define MAX_PATTERN_LEN 64
#define MAX_DNA_LEN 8

// DNA commands
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
  switch(g) {
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

class Mutator {
public:
  Mutator(uint8_t id) {
    generate_dna(id);
  }

  void evolve(
    const char* input_pattern,
    const char* sender_dna,
    char* output_pattern // must be >= MAX_PATTERN_LEN
  ) 
  {
    uint8_t len = strlen(input_pattern);
    if (len > MAX_PATTERN_LEN) len = MAX_PATTERN_LEN;

    // Copy input → buffer
    for (uint8_t i = 0; i < len; i++) {
      buffer[i] = input_pattern[i];
    }
    buf_len = len;

    // apply mutations driven by both DNAs
    mutate_by_dna(sender_dna, chaos);
    mutate_by_dna(my_dna, chaos);

    // final sanitize
    if (buf_len < 1) { buffer[0] = '_'; buf_len = 1; }

    // output
    for (uint8_t i = 0; i < buf_len; i++) {
      output_pattern[i] = buffer[i];
    }
    output_pattern[buf_len] = '\0';
    chaos += 0.025f;
  }

  char* get_dna() {
    return my_dna;
  }

private:
  char buffer[MAX_PATTERN_LEN];
  uint8_t buf_len;
  char my_dna[MAX_DNA_LEN];
  float chaos = 0.0f;

  void generate_dna(uint8_t id) {
    const char geneSet[] = "ABCDEFG";
    const uint8_t geneCount = 7;

    // Choose DNA length between 3–5 deterministically
    uint8_t dnaLen = 3 + (id % 3); // IDs cycle through 3,4,5 length

    if (dnaLen >= MAX_DNA_LEN) dnaLen = MAX_DNA_LEN - 1;

    for (uint8_t i = 0; i < dnaLen; i++) {
      // 5 is a good mixing constant (prime relative to 7)
      uint8_t idx = (id * 5 + i * 3) % geneCount;
      my_dna[i] = geneSet[idx];
    }

    my_dna[dnaLen] = '\0';
  }

  void mutate_by_dna(const char* dna, float chaos) {
    uint8_t dlen = strlen(dna);
    if (dlen == 0) return;

    for (uint8_t i = 0; i < dlen; i++) {
      MutationType m = decode_gene(dna[i]);
      float p = 0.05 + chaos * 0.2; // mutation probability increases with chaos
      if (random(1000) / 1000.0 > p) continue; // skip

      apply_mutation(m);
    }
  }

  void apply_mutation(MutationType m) {
    switch(m) {
      case ADD_HIT: flip_random('_','x'); break;
      case REMOVE_HIT: flip_random('x','_'); break;
      case BURST_RESTS: insert_rest_burst(); break;
      case DUPLICATE_SEGMENT: duplicate_seg(); break;
      case SHUFFLE_SEGMENT: shuffle_small(); break;
      case STRETCH: stretch(); break;
      case COMPRESS: compress(); break;
      default: break;
    }
  }

  void flip_random(char from, char to) {
    for (uint8_t tries=0; tries<4; tries++) {
      uint8_t i = random(buf_len);
      if (buffer[i] == from) { buffer[i] = to; return; }
    }
  }

  void insert_rest_burst() {
    if (buf_len >= MAX_PATTERN_LEN-2) return;
    uint8_t pos = random(buf_len);
    for (int i = buf_len; i > pos; i--) buffer[i] = buffer[i-1];
    buffer[pos] = '_';
    buf_len++;
  }

  void duplicate_seg() {
    if (buf_len >= MAX_PATTERN_LEN-2) return;
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
    if (buf_len >= MAX_PATTERN_LEN-1) return;
    uint8_t pos = random(buf_len);
    for (int i = buf_len; i > pos; i--) buffer[i] = buffer[i-1];
    buffer[pos] = '_';
    buf_len++;
  }

  void compress() {
    if (buf_len <= 1) return;
    uint8_t pos = random(buf_len);
    for (uint8_t i = pos; i < buf_len-1; i++) buffer[i] = buffer[i+1];
    buf_len--;
  }
};
