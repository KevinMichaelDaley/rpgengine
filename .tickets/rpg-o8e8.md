---
id: rpg-o8e8
status: open
deps: [rpg-wsya]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-ssr8
tags: [aegis, vm, security]
---
# Aegis similarity hashing (SimHash, MinHash)

Implement similarity hashing per ref/aegis_bytecode_spec.md §4.2.

Two hash types for different use cases:
- SimHash: near-duplicate detection (scripts with small modifications)
- MinHash: resemblance detection (scripts sharing many substructures)

Input: canonicalized bytecode graph (nodes = instructions/registers/constants, edges = def-use/control/data).

Implement:
- aegis_simhash(bytecode): compute 64-bit SimHash from canonicalized bytecode features
- aegis_minhash(bytecode, num_hashes): compute MinHash signature (array of k hash values)
- aegis_similarity(hash_a, hash_b): Hamming distance for SimHash, Jaccard estimate for MinHash
- Feature extraction: each instruction → feature vector (opcode + operand types + def-use chain depth)

Files:
- include/ferrum/aegis/aegis_similarity.h
- src/aegis/aegis_simhash.c
- src/aegis/aegis_minhash.c
- tests/aegis/aegis_similarity_tests.c

Acceptance criteria:
- [ ] Identical bytecode → identical hashes
- [ ] Slightly modified bytecode → close hashes (low Hamming distance)
- [ ] Unrelated bytecode → distant hashes
- [ ] MinHash Jaccard estimate approximates true Jaccard similarity
- [ ] Tests: identical programs, one-instruction diff, completely different programs, MinHash accuracy

## Acceptance Criteria

SimHash and MinHash correctly detect near-duplicates and similar bytecode programs

