### 1\. Current Understanding of the Project



My current understanding is that the goal of the project is not merely to implement a parallel SAT solver, but to investigate what a non-deterministic computer could look like if implemented on finite physical resources.



Classical complexity theory defines a nondeterministic machine as one that can *"guess"* a correct computational path and verify it efficiently.



The key challenge therefore becomes:



How could such a "guessing" mechanism be represented or approximated in a real computer architecture?



Professor Loskot also suggested that the project should be approached from a top-down perspective, without initially worrying about physical implementation details.







### 2\. Possible Interpretations



At the moment, I see at least three possible interpretations of a nondeterministic computer.



**Model A: Explicit Branching**



A GUESS operation creates multiple execution paths.



Like:



&#x20;   GUESS x

    Path 1: x = 0
    Path 2: x = 1


The computation becomes a tree.



Advantages:

* simple
* closely follows textbook NTM definitions



Disadvantages:

* branch count grows exponentially
* quickly exceeds available resources





**Model B: Limited Physical Parallelism**



Execution paths are generated as above, but only a fixed number of physical resources are available.



Example:



1000000 logical paths -> 16 hardware slots



If so, the central problem becomes:

* scheduling
* partitioning the computation tree
* path migration
* resource allocation



This interpretation appears consistent with discussions about:

* concurrency
* computation tree partitioning
* finite resources





**Model C: Multiple Paths Within One Execution Context**



Instead of creating a separate execution state for every branch, multiple possibilities are represented together.



e.g. Instead of branching



&#x20;   x = 0

&#x20;   x = 1



store



&#x20;   x ∈ {0,1}



and refine possibilities as more constraints are encountered.



This approach appears related to:



* symbolic execution
* constraint propagation
* compressed state representations



This interpretation might also relate to the suggestion of computing multiple paths within one thread. (using *possibility*, but not execute it *probabilistically*.)







### 3\. Obstacles to Building a Non-deterministic Computer



**Obstacle 1: Branch Explosion**



After *n* binary guesses, **2^*n*** paths will potentially be explored. Though some branches might be early terminated, the scale still goes extremely fast w.r.t. *n*.



**Obstacle 2: Finite Hardware Resources**



Real systems have limited:

* CPU cores (for universal CPU)
* FPGA logic resources (for customed FPGA)
* memory (for all devices)



Thus physical parallelism is always bounded.





**Obstacle 3: State Storage**



Even if execution can be delayed or scheduled, the states themselves must still be represented somehow.



State storage might become the dominant bottleneck. It can be hard to find an efficient way to do that.





**Obstacle 4: Global Termination**



For many NP-style problems:

one successful path ⇒ terminate all other paths



This requires:

* detection
* synchronization
* communication



between active branches. This might be hard to implement.







### 4\. Current Experiments



So far I implemented two small SAT-based prototypes.



**Prototype 1: One thread per assignment**



Purpose: Explicit demonstration of branch explosion.





**Prototype 2: A small nondeterministic instruction model**



Including *GUESS*, *ASSERT*, *STOP*. which more closely follows the textbook nondeterministic machine abstraction.







### 5\. Questions for Discussion



1. Is the primary research goal: **(A) efficient scheduling of large computation trees under limited resources** or **(B) finding alternative ways to represent multiple computation paths simultaneously?**





2\. When discussing "multiple paths within one thread", what level of path sharing was envisioned?

* sequential exploration
* compressed representation
* something else





3\. Is SAT (or any other NP-C problem) intended only as a benchmark problem, or is it expected to play a central role in the project?





4\. Should the project focus primarily on:

* architecture design
* execution models
* scheduling algorithms
* ~~FPGA realization~~

during the next stage?

