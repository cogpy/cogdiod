# Neural Architectures - 61-Definition Periodic Tables

This directory contains documentation on the 61-Definition Periodic Tables framework for understanding and comparing five canonical neural network architectures.

## Overview

The 61-definition framework provides a systematic categorization of neural network components and behaviors using:

- **13 Primary Categories (Generators):** 1 Definitional + 2 Directional + 3 Dimensional + 7 Organizational
- **6 Secondary Categories (Composites):** Direction × Dimension combinations  
- **42 Coordinate Cells:** Direction × Dimension × Category intersections

## Documents

### [The Complete 61-Definition Periodic Tables](The_Complete_61_Definition_Periodic_Tables.md)

The comprehensive document containing all five architectures with detailed comparison tables and evolutionary analysis.

### [ANN & CNN Periodic Tables](The_61_Definition_Periodic_Tables_ANN_CNN.md)

Detailed periodic tables for:
- **Feedforward ANN (MLP):** The canonical baseline model
- **Convolutional Neural Network (CNN):** Spatial feature extraction with local kernels

### [RNN, GNN & Transformer Periodic Tables](The_61_Definition_Periodic_Tables_RNN_GNN_Transformer.md)

Detailed periodic tables for:
- **Recurrent Neural Network (RNN/LSTM):** Temporal memory via local hidden state loops
- **Graph Neural Network (GNN):** Dynamic topology via message passing
- **Transformer:** Self-attention with dynamic attention scores

## Key Insights

Each architecture innovates on specific categories from the canonical ANN:

| Architecture | Primary Innovation |
|--------------|-------------------|
| CNN | Conjunction (local kernel) + Recursion (weight sharing) |
| RNN | Recursion (moves from Global to Local) |
| GNN | Disjunction (dynamic topology) |
| Transformer | Conjunction (self-referential attention) |

## Integration with CogDiod

The 61-definition framework provides a formal foundation for understanding how neural architectures can be mapped to CogDiod's cognitive primitives:

- **Categories 7-13** (Organizational) map to atom types and link semantics
- **Categories 14-19** (Composites) map to spatial/temporal/causal relationships
- **Cells 20-61** represent specific computational and structural elements

This framework supports CogDiod's goal of providing a unified cognitive architecture that can incorporate insights from diverse neural network approaches.
