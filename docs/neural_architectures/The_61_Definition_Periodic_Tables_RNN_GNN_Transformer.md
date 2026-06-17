# The 61-Definition Periodic Tables: RNN, GNN, & Transformer

This document constructs the complete 61-definition periodic tables for Recurrent Neural Networks (RNN), Graph Neural Networks (GNN), and Transformers.

---

## 3. Recurrent Neural Network (RNN / LSTM)

The RNN alters the canonical model by moving Recursion (Category 13/7) from the Global dimension into the Local dimension, creating an internal memory loop.

### The 13 Primary Categories (Generators)
*(1-6 remain identical to ANN)*
7.  **Existence ($\exists$):** Sequence element $x_t$.
8.  **Distinction ($\neg$):** Current vs. Previous state.
9.  **Disjunction ($\vee$):** Time step separation.
10. **Conjunction ($\wedge$):** Dual integration (Input + Hidden).
11. **Transition ($\rightarrow$):** Hidden state update $h_t$.
12. **Modular Closure ($\leftrightarrow$):** Sequence output.
13. **Recursion ($\forall$):** **Hidden state fed back as input (Local Loop).**

### The 6 Dimensional-Directional Composites
*(14-19 remain identical to ANN)*

### The 42 Coordinate Cells (The Periodic Table)
*(Cells that differ significantly from ANN are bolded)*

| Category | Local Structure (14) | Local Process (15) | Local Control (16) | Global Space (17) | Global Time (18) | Global Causality (19) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Existence (7)** | 20. **$x_t$ AND $h_{t-1}$** | 27. Pulse at time $t$ | 34. Pre-activation state | 41. **Input Sequence** | 48. Forward pass starts | 55. Target Sequence |
| **Distinction (8)** | 21. Active/Inactive | 28. Firing/Silence | 35. Threshold ($\theta$) | 42. Output vs Target | 49. Error detected | 56. Error magnitude |
| **Disjunction (9)** | 22. **Time step $t$ vs $t+1$** | 29. **Unrolled nodes** | 36. Below threshold | 43. Orthogonal features | 50. Divergent gradients | 57. Gradient vector |
| **Conjunction (10)** | 23. **$W_{xh}$ and $W_{hh}$** | 30. Weighted sum | 37. Integration | 44. Weight Matrices | 51. Batch aggregation | 58. Loss Function |
| **Transition (11)** | 24. **Hidden State $h_t$** | 31. Signal propagation | 38. Transfer function | 45. Hidden Layers | 52. **BPTT starts** | 59. Chain rule |
| **Closure (12)** | 25. Output Node $y_t$ | 32. Final activation | 39. Output state | 46. Output Sequence | 53. Weight update | 60. Minimized Loss |
| **Recursion (13)** | 26. **$h_t \rightarrow h_{t-1}$ loop** | 33. **Next time step** | 40. **State preservation** | 47. Network architecture | 54. Next Epoch begins | 61. Optimizer step |

---

## 4. Graph Neural Network (GNN)

The GNN alters the canonical model by making Disjunction (Topology) a dynamic input, rather than a fixed architectural constraint.

### The 13 Primary Categories (Generators)
*(1-6 remain identical to ANN)*
7.  **Existence ($\exists$):** Node feature $x_v$.
8.  **Distinction ($\neg$):** Node vs Edge.
9.  **Disjunction ($\vee$):** **Adjacency Matrix (Explicit Topology).**
10. **Conjunction ($\wedge$):** **Message Passing along edges.**
11. **Transition ($\rightarrow$):** Node embedding update.
12. **Modular Closure ($\leftrightarrow$):** Graph-level readout (Pooling).
13. **Recursion ($\forall$):** Layer-wise message propagation.

### The 6 Dimensional-Directional Composites
*(14-19 remain identical to ANN)*

### The 42 Coordinate Cells (The Periodic Table)
*(Cells that differ significantly from ANN are bolded)*

| Category | Local Structure (14) | Local Process (15) | Local Control (16) | Global Space (17) | Global Time (18) | Global Causality (19) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Existence (7)** | 20. **Node $v$ & Edge $e$** | 27. Input pulse | 34. Pre-activation state | 41. **Graph $G=(V,E)$** | 48. Forward pass starts | 55. Target Vector |
| **Distinction (8)** | 21. Connected/Unconnected | 28. Firing/Silence | 35. Threshold ($\theta$) | 42. Output vs Target | 49. Error detected | 56. Error magnitude |
| **Disjunction (9)** | 22. **Neighborhood $N(v)$** | 29. **Adjacency filter** | 36. Below threshold | 43. **Adjacency Matrix** | 50. Divergent gradients | 57. Gradient vector |
| **Conjunction (10)** | 23. **Message Function** | 30. **Aggregation (Sum/Mean)** | 37. Integration | 44. Weight Matrix | 51. Batch aggregation | 58. Loss Function |
| **Transition (11)** | 24. **Updated Node $h_v^{(k)}$** | 31. **Update Function** | 38. Transfer function | 45. GNN Layers | 52. Backpropagation | 59. Chain rule |
| **Closure (12)** | 25. Output Node | 32. Final activation | 39. Output state | 46. **Graph Readout** | 53. Weight update | 60. Minimized Loss |
| **Recursion (13)** | 26. $h_v^{(k)} \rightarrow h_v^{(k+1)}$ | 33. Next layer triggers | 40. Iterative firing | 47. Network architecture | 54. Next Epoch begins | 61. Optimizer step |

---

## 5. Transformer (Self-Attention)

The Transformer represents a leap in Conjunction (Category 10/4). Instead of static weights, the connection strengths are dynamically computed from the data itself (Autognosis at the local level).

### The 13 Primary Categories (Generators)
*(1-6 remain identical to ANN)*
7.  **Existence ($\exists$):** Token embedding + Positional encoding.
8.  **Distinction ($\neg$):** Query, Key, Value projections.
9.  **Disjunction ($\vee$):** Masking (causal or padding).
10. **Conjunction ($\wedge$):** **Dynamic Attention Scores ($QK^T$).**
11. **Transition ($\rightarrow$):** Multi-head subspace projection.
12. **Modular Closure ($\leftrightarrow$):** LayerNorm + Residual connection.
13. **Recursion ($\forall$):** Stacked encoder/decoder blocks.

### The 6 Dimensional-Directional Composites
*(14-19 remain identical to ANN)*

### The 42 Coordinate Cells (The Periodic Table)
*(Cells that differ significantly from ANN are bolded)*

| Category | Local Structure (14) | Local Process (15) | Local Control (16) | Global Space (17) | Global Time (18) | Global Causality (19) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Existence (7)** | 20. **Token + Pos. Enc.** | 27. Input pulse | 34. Pre-activation state | 41. **Context Window** | 48. Forward pass starts | 55. Target Sequence |
| **Distinction (8)** | 21. **$Q, K, V$ vectors** | 28. Firing/Silence | 35. Threshold ($\theta$) | 42. Output vs Target | 49. Error detected | 56. Error magnitude |
| **Disjunction (9)** | 22. **Attention Mask** | 29. **Causal blocking** | 36. Below threshold | 43. Orthogonal features | 50. Divergent gradients | 57. Gradient vector |
| **Conjunction (10)** | 23. **Attention Matrix** | 30. **Scaled Dot-Product** | 37. **Softmax ($QK^T$)** | 44. $W_Q, W_K, W_V$ | 51. Batch aggregation | 58. Loss Function |
| **Transition (11)** | 24. **Multi-Head concat** | 31. **Value weighting** | 38. Transfer function | 45. Transformer Blocks | 52. Backpropagation | 59. Chain rule |
| **Closure (12)** | 25. **Residual + Norm** | 32. **Add & Norm** | 39. Output state | 46. Output Sequence | 53. Weight update | 60. Minimized Loss |
| **Recursion (13)** | 26. Output becomes input | 33. Next block triggers | 40. Iterative firing | 47. Network architecture | 54. Next Epoch begins | 61. Optimizer step |
