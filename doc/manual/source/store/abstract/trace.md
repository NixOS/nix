# deps

adrv <- bdrv


# build trace

bdrv -> brez

adrv : Derivation Derivation

adrv[bdrv => brez] : Derivation (Set StoreObject)


# small step map

type SmalStepMap = Map (Derivation (Set StoreObject)) StoreObject

Map.fromList [adrv[bdrv => brez] -> arez] : SmallStepMap


# big step map (with admissibility rules)

type BigStepMap = Map (Derivation Derivation) (StoreObject, BigStepMap)

Map.fromList [adrv -> arez (depends on [bdrv -> brez])}] : BigStepMap
