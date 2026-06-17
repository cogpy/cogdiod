# The Complete 61-Definition Periodic Tables for Five Neural Architectures

## Overview

This document constructs the full 61-definition periodic table for each of the five canonical neural network architectures: ANN (MLP), CNN, RNN, GNN, and Transformer. Each table fills all 13 primary generators + 6 composites + 42 coordinate cells, showing precisely where each architecture diverges from the canonical ANN.

The 61 definitions consist of:
- **1 Definitional Category** (Autognosis)
- **2 Directional Categories** (Local, Global)
- **3 Dimensional Categories** (Spatiality, Temporality, Causality)
- **7 Organizational Categories** (Existence through Recursion)
- **6 Composites** (Direction × Dimension)
- **42 Cells** (Direction × Dimension × Category)

---

## Architecture I: Feedforward ANN (Multilayer Perceptron)

*The canonical baseline from Winiwarter (1999). All other architectures are variations on this theme.*

### Primary Categories (Generators 1-13)

| # | Category | ANN Instantiation |
|---|---|---|
| 1 | Autognosis | The network builds a statistical self-image of the data distribution via gradient descent |
| 2 | Local (Reductionist) | Single neuron computations (bottom-up integration of inputs) |
| 3 | Global (Holistic) | Network-level loss landscape (top-down differentiation of error) |
| 4 | Spatiality | The topology of connections (architecture) |
| 5 | Temporality | The dynamics of activation (forward/backward pass) |
| 6 | Causality | The regulatory logic (loss, gradients, optimization) |
| 7 | Existence | A node or a datum |
| 8 | Distinction | Binary state (active/inactive) or error signal |
| 9 | Disjunction | Independence or separation |
| 10 | Conjunction | Dense coupling (weight multiplication) |
| 11 | Transition | Hierarchical layering |
| 12 | Modular Closure | Boundary formation (output layer) |
| 13 | Recursion | Epoch cycling (output feeds next training iteration) |

### Composites (14-19)

| # | Composite | ANN Instantiation |
|---|---|---|
| 14 | Local Structure | The neuron: inputs, weights, bias |
| 15 | Local Process | The activation event of a single neuron |
| 16 | Local Control | The activation function (sigmoid, ReLU) |
| 17 | Global Space | The layer topology (width × depth) |
| 18 | Global Time | The epoch trajectory through weight space |
| 19 | Global Causality | The loss function + optimizer algorithm |

### The 42 Cells (20-61)

| Cat. | Local Structure | Local Process | Local Control | Global Space | Global Time | Global Causality |
|---|---|---|---|---|---|---|
| **Exist.** | 20. Input node $x_i$ | 27. Input pulse arrives | 34. Pre-activation state | 41. Input vector $\mathbf{x}$ | 48. Forward pass initiates | 55. Target vector $\mathbf{t}$ |
| **Dist.** | 21. Active/Inactive | 28. Firing/Silence | 35. Threshold $\theta$ | 42. Output $\neq$ Target | 49. Error event | 56. Error magnitude $E$ |
| **Disj.** | 22. Disconnected nodes | 29. Independent pulses | 36. Sub-threshold decay | 43. Orthogonal features | 50. Gradient components | 57. Gradient vector $\nabla E$ |
| **Conj.** | 23. Weight $w_{ij}$ | 30. Weighted sum $\Sigma w_i x_i$ | 37. Threshold exceeded | 44. Weight matrix $\mathbf{W}$ | 51. Batch accumulation | 58. Loss function $\mathcal{L}$ |
| **Trans.** | 24. Hidden node | 31. Signal propagates | 38. Transfer function | 45. Hidden layers | 52. Backpropagation | 59. Chain rule $\partial E/\partial w$ |
| **Clos.** | 25. Output node $y_j$ | 32. Final activation | 39. Saturated state | 46. Output vector $\mathbf{y}$ | 53. Weight update $\Delta w$ | 60. Loss minimized |
| **Recur.** | 26. $y_j$ → next input | 33. Next layer fires | 40. Reset for next datum | 47. Architecture fixed | 54. Next epoch begins | 61. Learning rate decay |

---

## Architecture II: Convolutional Neural Network (CNN)

*Alters Categories 10 (Conjunction → Local Kernel) and 13 (Recursion → Weight Sharing/Translation Invariance).*

### Primary Categories (Generators 1-13)

| # | Category | CNN Instantiation | Differs from ANN? |
|---|---|---|---|
| 1 | Autognosis | Builds spatial feature hierarchy of input | Same principle |
| 2 | Local | Single kernel application (receptive field) | **Restricted locality** |
| 3 | Global | Feature map ensemble + loss landscape | Same principle |
| 4 | Spatiality | Grid topology (2D/3D spatial structure) | **Spatial grid** |
| 5 | Temporality | Convolution sweep + pooling sequence | **Sweep dynamics** |
| 6 | Causality | Loss + spatial invariance constraints | Same principle |
| 7 | Existence | **Pixel/Voxel** | **Changed** |
| 8 | Distinction | Edge/No-edge (local contrast) | **Changed** |
| 9 | Disjunction | **Stride separation** | **Changed** |
| 10 | Conjunction | **Local kernel (receptive field)** | **Changed** |
| 11 | Transition | **Feature map channel** | **Changed** |
| 12 | Modular Closure | Flatten to dense vector | **Changed** |
| 13 | Recursion | **Weight sharing across positions** | **Changed** |

### Composites (14-19)

| # | Composite | CNN Instantiation |
|---|---|---|
| 14 | Local Structure | The kernel: a small weight matrix applied to a local patch |
| 15 | Local Process | The convolution operation (sliding dot product) |
| 16 | Local Control | ReLU activation after convolution |
| 17 | Global Space | The feature map stack (channels × height × width) |
| 18 | Global Time | The hierarchical extraction pipeline (edges → textures → objects) |
| 19 | Global Causality | Cross-entropy loss + data augmentation invariance |

### The 42 Cells (20-61)

| Cat. | Local Structure | Local Process | Local Control | Global Space | Global Time | Global Causality |
|---|---|---|---|---|---|---|
| **Exist.** | 20. **Pixel** $p_{ij}$ | 27. Patch activation | 34. Pre-activation | 41. **Image tensor** $H{\times}W{\times}C$ | 48. Forward pass | 55. Target label |
| **Dist.** | 21. **Edge/No-edge** | 28. Firing/Silence | 35. Threshold $\theta$ | 42. Predicted $\neq$ Target | 49. Error event | 56. Error magnitude |
| **Disj.** | 22. **Stride gaps** | 29. **Non-overlapping patches** | 36. Sub-threshold | 43. **Channel independence** | 50. Gradient per filter | 57. Gradient vector |
| **Conj.** | 23. **Kernel** $k{\times}k$ | 30. **Convolution** $\sum k \cdot p$ | 37. Threshold exceeded | 44. **Filter bank** | 51. Batch accumulation | 58. Loss function |
| **Trans.** | 24. **Feature map node** | 31. **Max/Avg Pooling** | 38. ReLU | 45. **Conv-Pool layers** | 52. Backpropagation | 59. Chain rule |
| **Clos.** | 25. **Flattened node** | 32. Final activation | 39. Saturated state | 46. **Flattened vector** | 53. Weight update | 60. Loss minimized |
| **Recur.** | 26. **Shared weights** | 33. **Translation invariance** | 40. **Equivariance** | 47. Architecture fixed | 54. Next epoch | 61. LR decay |

---

## Architecture III: Recurrent Neural Network (RNN / LSTM)

*Moves Category 13 (Recursion) from Global Time into Local Structure/Process — creating an internal temporal loop.*

### Primary Categories (Generators 1-13)

| # | Category | RNN Instantiation | Differs from ANN? |
|---|---|---|---|
| 1 | Autognosis | Builds temporal self-image of sequential data | Same principle |
| 2 | Local | Single cell computation at time $t$ | **Includes memory** |
| 3 | Global | Sequence-level loss landscape | Same principle |
| 4 | Spatiality | **Recurrent cell topology** (input + hidden state) | **Changed** |
| 5 | Temporality | **Unrolled time steps** | **Changed** |
| 6 | Causality | Loss + temporal gradient flow | Same principle |
| 7 | Existence | **Sequence element $x_t$ + Hidden state $h_{t-1}$** | **Changed** |
| 8 | Distinction | Current vs. Previous | **Changed** |
| 9 | Disjunction | **Time step separation** ($t$ vs $t+1$) | **Changed** |
| 10 | Conjunction | **Dual weights** ($W_{xh}$, $W_{hh}$) | **Changed** |
| 11 | Transition | **Hidden state update** $h_t$ | **Changed** |
| 12 | Modular Closure | Sequence output $y_T$ | Same principle |
| 13 | Recursion | **$h_t$ fed back as $h_{t-1}$ (Local Loop)** | **Changed** |

### Composites (14-19)

| # | Composite | RNN Instantiation |
|---|---|---|
| 14 | Local Structure | The cell: input gate + hidden state + output gate (LSTM) |
| 15 | Local Process | State update at a single time step |
| 16 | Local Control | Forget gate / tanh activation |
| 17 | Global Space | The unrolled sequence topology |
| 18 | Global Time | The sequence trajectory through hidden state space |
| 19 | Global Causality | Sequence loss + BPTT gradient algorithm |

### The 42 Cells (20-61)

| Cat. | Local Structure | Local Process | Local Control | Global Space | Global Time | Global Causality |
|---|---|---|---|---|---|---|
| **Exist.** | 20. **$x_t$ AND $h_{t-1}$** | 27. Pulse at time $t$ | 34. Pre-activation | 41. **Input sequence** | 48. Forward pass | 55. **Target sequence** |
| **Dist.** | 21. **Present/Past** | 28. Firing/Silence | 35. Threshold | 42. Output $\neq$ Target | 49. Error event | 56. Error magnitude |
| **Disj.** | 22. **$t$ vs $t+1$ gap** | 29. **Unrolled steps** | 36. **Forget gate** | 43. **Sequence positions** | 50. **Vanishing gradient** | 57. Gradient vector |
| **Conj.** | 23. **$W_{xh}$, $W_{hh}$** | 30. Weighted sum | 37. **Input gate** | 44. Weight matrices | 51. Batch accumulation | 58. Loss function |
| **Trans.** | 24. **Hidden $h_t$** | 31. **State update** | 38. **tanh/sigmoid** | 45. Stacked layers | 52. **BPTT** | 59. Chain rule |
| **Clos.** | 25. Output $y_t$ | 32. Final activation | 39. **Output gate** | 46. Output sequence | 53. Weight update | 60. Loss minimized |
| **Recur.** | 26. **$h_t \rightarrow h_{t-1}$** | 33. **Next $t$ fires** | 40. **State carry** | 47. Architecture fixed | 54. Next epoch | 61. LR decay |

---

## Architecture IV: Graph Neural Network (GNN)

*Makes Category 9 (Disjunction/Topology) a dynamic input rather than a fixed architectural constraint.*

### Primary Categories (Generators 1-13)

| # | Category | GNN Instantiation | Differs from ANN? |
|---|---|---|---|
| 1 | Autognosis | Builds relational self-image of graph-structured data | Same principle |
| 2 | Local | Single node computation | **Node-centric** |
| 3 | Global | Graph-level readout + loss landscape | **Graph-level** |
| 4 | Spatiality | **Non-Euclidean graph topology** | **Changed** |
| 5 | Temporality | Message passing rounds | **Changed** |
| 6 | Causality | Loss + graph-invariance constraints | Same principle |
| 7 | Existence | **Node $v$ with features $x_v$** | **Changed** |
| 8 | Distinction | **Node vs Edge** | **Changed** |
| 9 | Disjunction | **Adjacency Matrix $A$ (Dynamic Topology)** | **Changed** |
| 10 | Conjunction | **Message Passing along edges** | **Changed** |
| 11 | Transition | **Node embedding update** | **Changed** |
| 12 | Modular Closure | **Graph-level readout** | **Changed** |
| 13 | Recursion | Layer-wise propagation ($k$ rounds) | Same principle |

### Composites (14-19)

| # | Composite | GNN Instantiation |
|---|---|---|
| 14 | Local Structure | The node: features + edge connections to neighbors |
| 15 | Local Process | Message aggregation from neighborhood $N(v)$ |
| 16 | Local Control | Aggregation function (sum/mean/max) |
| 17 | Global Space | The graph $G = (V, E, A)$ |
| 18 | Global Time | The $K$-hop message propagation sequence |
| 19 | Global Causality | Graph-level loss + permutation invariance |

### The 42 Cells (20-61)

| Cat. | Local Structure | Local Process | Local Control | Global Space | Global Time | Global Causality |
|---|---|---|---|---|---|---|
| **Exist.** | 20. **Node $v$, Edge $e$** | 27. Message arrives | 34. Pre-aggregation | 41. **Graph $G=(V,E)$** | 48. Forward pass | 55. Target (node/graph) |
| **Dist.** | 21. **Connected/Not** | 28. Message/No-message | 35. Threshold | 42. Output $\neq$ Target | 49. Error event | 56. Error magnitude |
| **Disj.** | 22. **$N(v)$ boundary** | 29. **Adjacency filter** | 36. **Degree normalization** | 43. **Adjacency $A$** | 50. Gradient per node | 57. Gradient vector |
| **Conj.** | 23. **Message function** | 30. **Aggregate $\sum_{u \in N(v)}$** | 37. Integration | 44. **Weight matrix** | 51. Batch accumulation | 58. Loss function |
| **Trans.** | 24. **$h_v^{(k)}$ embedding** | 31. **Update function** | 38. Transfer (ReLU) | 45. **GNN layers** | 52. Backpropagation | 59. Chain rule |
| **Clos.** | 25. Final node embed. | 32. Final activation | 39. Saturated state | 46. **Readout vector** | 53. Weight update | 60. Loss minimized |
| **Recur.** | 26. $h_v^{(k)} \rightarrow h_v^{(k+1)}$ | 33. Next round fires | 40. Iterative refine | 47. Architecture fixed | 54. Next epoch | 61. LR decay |

---

## Architecture V: Transformer (Self-Attention)

*Makes Category 10 (Conjunction) self-referential — the weights are dynamically computed from the data itself. This is Autognosis at the local level.*

### Primary Categories (Generators 1-13)

| # | Category | Transformer Instantiation | Differs from ANN? |
|---|---|---|---|
| 1 | Autognosis | **Data attends to itself** — the input computes its own connectivity | **Deepened** |
| 2 | Local | Single attention head computation | Same principle |
| 3 | Global | Context window + loss landscape | Same principle |
| 4 | Spatiality | **Dynamic attention topology** (fully connected but weighted) | **Changed** |
| 5 | Temporality | Parallel token processing (no sequential constraint) | **Changed** |
| 6 | Causality | Loss + attention masking constraints | Same principle |
| 7 | Existence | **Token embedding + Positional encoding** | **Changed** |
| 8 | Distinction | **$Q$, $K$, $V$ projections** | **Changed** |
| 9 | Disjunction | **Attention Mask (causal/padding)** | **Changed** |
| 10 | Conjunction | **Dynamic Attention Scores $\text{softmax}(QK^T/\sqrt{d})$** | **Changed** |
| 11 | Transition | **Multi-Head parallel subspaces** | **Changed** |
| 12 | Modular Closure | **LayerNorm + Residual connection** | **Changed** |
| 13 | Recursion | Stacked Transformer blocks | Same principle |

### Composites (14-19)

| # | Composite | Transformer Instantiation |
|---|---|---|
| 14 | Local Structure | The attention head: $Q$, $K$, $V$ projections + score matrix |
| 15 | Local Process | Scaled dot-product attention computation |
| 16 | Local Control | Softmax normalization + masking |
| 17 | Global Space | The context window (sequence length × embedding dimension) |
| 18 | Global Time | The layer-by-layer refinement of representations |
| 19 | Global Causality | Cross-entropy loss + KV-cache optimization |

### The 42 Cells (20-61)

| Cat. | Local Structure | Local Process | Local Control | Global Space | Global Time | Global Causality |
|---|---|---|---|---|---|---|
| **Exist.** | 20. **Token + Pos.Enc.** | 27. Token enters | 34. Pre-attention state | 41. **Context window** | 48. Forward pass | 55. Target sequence |
| **Dist.** | 21. **$Q$, $K$, $V$** | 28. Projection event | 35. **Scaling $1/\sqrt{d}$** | 42. Output $\neq$ Target | 49. Error event | 56. Error magnitude |
| **Disj.** | 22. **Attention Mask** | 29. **Causal blocking** | 36. **Masked positions** | 43. **Head independence** | 50. Gradient per head | 57. Gradient vector |
| **Conj.** | 23. **Attention Score** | 30. **$QK^T$ dot product** | 37. **Softmax** | 44. **$W_Q, W_K, W_V, W_O$** | 51. Batch accumulation | 58. Loss function |
| **Trans.** | 24. **Multi-head concat** | 31. **Value weighting** | 38. **FFN (2-layer)** | 45. **Transformer blocks** | 52. Backpropagation | 59. Chain rule |
| **Clos.** | 25. **Residual + Norm** | 32. **Add & Norm** | 39. **Stabilized output** | 46. Output sequence | 53. Weight update | 60. Loss minimized |
| **Recur.** | 26. Block output → next | 33. Next block fires | 40. **KV-Cache** | 47. Architecture fixed | 54. Next epoch | 61. LR decay |

---

## Comparative Synthesis: Where Each Architecture Innovates

The following table shows which of the 13 primary categories each architecture fundamentally alters relative to the canonical ANN:

| Generator | ANN | CNN | RNN | GNN | Transformer |
|---|---|---|---|---|---|
| 1. Autognosis | Baseline | Same | Same | Same | **Deepened** |
| 7. Existence | Node | **Pixel** | **$x_t + h_{t-1}$** | **Node + Edge** | **Token + Pos** |
| 8. Distinction | Active/Inactive | **Edge/No-edge** | **Present/Past** | **Connected/Not** | **$Q$/$K$/$V$** |
| 9. Disjunction | Independence | **Stride** | **Time gap** | **Adjacency** | **Mask** |
| 10. Conjunction | **Dense $W$** | **Local Kernel** | **Dual $W$** | **Message** | **Dynamic $QK^T$** |
| 11. Transition | Hidden layer | **Feature map** | **$h_t$ update** | **Embedding update** | **Multi-head** |
| 12. Closure | Output layer | **Flatten** | Sequence output | **Graph readout** | **Residual+Norm** |
| 13. Recursion | Epoch loop | **Weight sharing** | **Local $h$ loop** | Layer rounds | Block stacking |

### The Evolutionary Trajectory

Reading this table as a directed sequence reveals the history of deep learning as a systematic exploration of the 7 organizational categories:

1. **ANN (1958-1986):** Establishes the baseline — all categories at their simplest form.
2. **CNN (1989):** Innovates at **Conjunction** (local kernel) and **Recursion** (weight sharing = translation invariance).
3. **RNN (1990):** Innovates at **Recursion** — moves it from Global to Local, creating temporal memory.
4. **GNN (2009):** Innovates at **Disjunction** — makes topology a dynamic input rather than a fixed constraint.
5. **Transformer (2017):** Innovates at **Conjunction** — makes it self-referential (autognostic). The data computes its own weights.

### Prediction: The Next Architecture

Following the pattern, the next breakthrough should make **Existence** (Category 7) itself dynamic and self-referential. Instead of fixed tokens/nodes/pixels as elementary units, the network would dynamically *discover* what constitutes an elementary unit from raw, unstructured input. This corresponds to:

> "What is a token?" becomes a learned, data-dependent question rather than a preprocessing decision.

This is precisely what recent work on **dynamic tokenization**, **patch merging** (in Vision Transformers), and **learned segmentation** is exploring — confirming the predictive power of the periodic table framework.
