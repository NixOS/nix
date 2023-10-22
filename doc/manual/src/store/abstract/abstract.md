# The abstract model

The abstract model is inspired by functional programming in the following key ways:

- Store objects are immutable.

- References to store objects are not under direct user control.
  References cannot be "forged" or "spoofed", nor can they by precisely controlled by fine-tuning the store paths to be referenced.

- References are *capabilities* in that objects unreachable by references of the inputs of any "operation" cannot possibly effect that operation.

- Newly store objects creates "fresh" store objects, never conflicting with one another.
  For example, manually added store paths will never conflict with one another or with ones built from a plan.
  Furthermore, two different plans will never "clash" trying produce the same store object.
  New builds will not only avoid messing up old builds, but they can also be confident no future build will mess up them.

These properties have many positive ramifications, which we will go over in detail in the sections on the abstract model.
