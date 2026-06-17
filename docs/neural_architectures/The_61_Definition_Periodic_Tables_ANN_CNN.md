# The 61-Definition Periodic Tables: ANN & CNN

This document constructs the complete 61-definition periodic tables for the canonical Feedforward Artificial Neural Network (ANN) and the Convolutional Neural Network (CNN).

The 61 definitions consist of:
*   **13 Primary Categories (Generators):** 1 Definitional + 2 Directional + 3 Dimensional + 7 Organizational.
*   **6 Secondary Categories (Composites):** The 2 Directional × 3 Dimensional combinations.
*   **42 Secondary Categories (Cells):** The 2 × 3 × 7 coordinate intersections.

---

## 1. Feedforward ANN (The Canonical Model)

### The 13 Primary Categories (Generators)
1.  **Autognosis:** The network's capacity to build a statistical self-image of the data distribution.
2.  **Local (Reductionist):** The neuron-level computations (bottom-up integration).
3.  **Global (Holistic):** The network-level loss landscape (top-down differentiation).
4.  **Spatiality:** The topological architecture (layers, nodes, weights).
5.  **Temporality:** The kinematic sequence (forward pass, backward pass).
6.  **Causality:** The regulatory rules (activation functions, loss optimization).
7.  **Existence (∃):** The elementary unit.
8.  **Distinction (¬):** Binary state or threshold.
9.  **Disjunction (∨):** Separation or independence.
10. **Conjunction (∧):** Coupling or dense connection.
11. **Transition (→):** Hierarchical branching.
12. **Modular Closure (↔):** Boundary formation.
13. **Recursion (∀):** Self-similar repetition.

### The 6 Dimensional-Directional Composites
14. **Local Spatiality → Structure:** The neuron and its immediate connections.
15. **Local Temporality → Process:** The activation event of a single neuron.
16. **Local Causality → Control:** The activation function regulating the neuron.
17. **Global Spatiality → Space:** The layer topology and overall architecture.
18. **Global Temporality → Time:** The epoch cycle and trajectory through latent space.
19. **Global Causality → Causality:** The loss function and gradient descent algorithm.

### The 42 Coordinate Cells (The Periodic Table)

| Category | Local Structure (14) | Local Process (15) | Local Control (16) | Global Space (17) | Global Time (18) | Global Causality (19) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Existence (7)** | 20. Input Node (x_i) | 27. Input pulse | 34. Pre-activation state | 41. Input Vector (**x**) | 48. Forward pass starts | 55. Target Vector (**t**) |
| **Distinction (8)** | 21. Active/Inactive state | 28. Firing/Silence | 35. Threshold (θ) | 42. Output vs Target | 49. Error detected | 56. Error magnitude (E) |
| **Disjunction (9)** | 22. Disconnected nodes | 29. Independent pulses | 36. Below threshold | 43. Orthogonal features | 50. Divergent gradients | 57. Gradient vector (∇E) |
| **Conjunction (10)** | 23. Dense Weights (w_ij) | 30. Weighted sum (Σ) | 37. Integration (E > θ) | 44. Weight Matrix (**W**) | 51. Batch aggregation | 58. Loss Function |
| **Transition (11)** | 24. Hidden Node | 31. Signal propagation | 38. Transfer function | 45. Hidden Layers | 52. Backpropagation | 59. Chain rule application |
| **Closure (12)** | 25. Output Node (y_j) | 32. Final activation | 39. Output state | 46. Output Vector (**y**) | 53. Weight update | 60. Minimized Loss state |
| **Recursion (13)** | 26. Output becomes input | 33. Next layer triggers | 40. Iterative firing | 47. Network architecture | 54. Next Epoch begins | 61. Optimizer step |

---

## 2. Convolutional Neural Network (CNN)

The CNN alters the canonical model by restricting Conjunction (dense weights become local kernels) and altering Transition (hidden layers become feature maps with pooling).

### The 13 Primary Categories (Generators)
*(1-6 remain identical to ANN)*
7.  **Existence (∃):** Pixel or voxel.
8.  **Distinction (¬):** Edge detection.
9.  **Disjunction (∨):** Spatial separation (stride).
10. **Conjunction (∧):** **Local receptive field (Kernel).**
11. **Transition (→):** **Feature map channel.**
12. **Modular Closure (↔):** Flattening to dense vector.
13. **Recursion (∀):** **Translation invariance (Weight sharing).**

### The 6 Dimensional-Directional Composites
*(14-19 remain identical to ANN, but applied to spatial grids)*

### The 42 Coordinate Cells (The Periodic Table)
*(Cells that differ significantly from ANN are bolded)*

| Category | Local Structure (14) | Local Process (15) | Local Control (16) | Global Space (17) | Global Time (18) | Global Causality (19) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Existence (7)** | 20. **Pixel/Voxel** | 27. Input pulse | 34. Pre-activation state | 41. **Image Tensor** | 48. Forward pass starts | 55. Target Vector |
| **Distinction (8)** | 21. Pixel intensity | 28. Firing/Silence | 35. Threshold (θ) | 42. Output vs Target | 49. Error detected | 56. Error magnitude |
| **Disjunction (9)** | 22. **Stride separation** | 29. Non-overlapping patches | 36. Below threshold | 43. Orthogonal features | 50. Divergent gradients | 57. Gradient vector |
| **Conjunction (10)** | 23. **Kernel/Filter** | 30. **Convolution (dot prod)** | 37. Integration | 44. **Filter Bank** | 51. Batch aggregation | 58. Loss Function |
| **Transition (11)** | 24. **Feature Map node** | 31. **Pooling (downsample)** | 38. Transfer (ReLU) | 45. **Conv/Pool Layers** | 52. Backpropagation | 59. Chain rule |
| **Closure (12)** | 25. Output Node | 32. Final activation | 39. Output state | 46. **Flattened Vector** | 53. Weight update | 60. Minimized Loss |
| **Recursion (13)** | 26. **Shared Weights** | 33. **Translation invariance** | 40. Iterative firing | 47. Network architecture | 54. Next Epoch begins | 61. Optimizer step |
