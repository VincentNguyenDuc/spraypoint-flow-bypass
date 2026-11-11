# Feedbacks

## Project Proposal

The proposal should clarify what "small flows" are and how they are identified. Look at the literature for some ideas on this front.

Regarding the execution plan, for the question of interest, Mininet might be an overkill. Consider using/adapting a pure simulator like ns3.

## Project Progress Update

I’d encourage you to go beyond empirical work and do some analytical modeling. See if you can modify the RNG models such that you can predict the change in oversubscription as the number of short flows changes. Compare your models to simulations.

Either way, it’ll be helpful to come up with an overall policy informed by your work about when and how much to bypass for optimal performance.

You might also consider load aware bypassing. If routerA is sending too much short flow traffic on linkA, it can send less long flow traffic on linkA.
