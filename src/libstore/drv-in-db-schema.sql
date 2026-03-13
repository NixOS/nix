-- Core derivation metadata (1:1 with ValidPaths for .drv store paths)
create table if not exists Derivations (
    -- FK to ValidPaths(id)
    id                 integer primary key not null,
    -- FK to ValidPaths(path); must be a .drv path
    path               text unique not null
        check (path like '%.drv'),
    -- BasicDerivation::platform
    platform           text not null,
    -- BasicDerivation::builder
    builder            text not null,
    -- BasicDerivation::args
    args               jsonb not null default '[]'
        check (json_type(args) = 'array'),
    -- DerivationOutput variant type (applies to all outputs):
    --   0 = inputAddressed  (outputs have path)
    --   1 = caFixed         (outputs have method, hashAlgo, hash)
    --   2 = caFloating      (outputs have method, hashAlgo)
    --   3 = deferred        (outputs have no extra fields)
    --   4 = impure          (outputs have method, hashAlgo)
    outputType         integer not null
        check (outputType between 0 and 4),
    -- 0 = nullopt, 1 = Some({...})
    -- Distinguishes "no structured attrs" from "empty structured attrs object"
    hasStructuredAttrs integer not null default 0
        check (hasStructuredAttrs in (0, 1)),
    unique (id, outputType),
    unique (id, hasStructuredAttrs),
    foreign key (id) references ValidPaths(id) on delete cascade,
    foreign key (path) references ValidPaths(path) on delete cascade
);

-- BasicDerivation::outputs (DerivationOutput variant)
create table if not exists DerivationOutputsV2 (
    drv        integer not null,
    -- output name, e.g. "out"
    id         text not null,
    -- must match parent Derivations.outputType (composite FK enforced)
    --
    -- That means is only pseduo-denormalized from Derivations, because
    -- the via composite FK ensures they never get out of sync.
    --
    -- Why do we do this? so per-row CHECK constraints can enforce the
    -- right columns per type. (Check constraints cannot follow foreign
    -- keys.)
    outputType integer not null
        check (outputType between 0 and 4),
    -- store path (inputAddressed only)
    path       text,
    -- ContentAddressMethod (caFixed/caFloating/impure)
    method     text,
    -- HashAlgorithm (caFixed/caFloating/impure)
    hashAlgo   text,
    -- hash value (caFixed only)
    hash       text,
    primary key (drv, id),
    foreign key (drv, outputType) references Derivations(id, outputType) on delete cascade,
    -- Enforce exactly one valid column shape per output type:
    check (
        (outputType = 0 -- inputAddressed
            and path is not null
            and method is null and hashAlgo is null and hash is null)
        or (outputType = 1 -- caFixed
            and path is null
            and method is not null and hashAlgo is not null and hash is not null)
        or (outputType = 2 -- caFloating
            and path is null and hash is null
            and method is not null and hashAlgo is not null)
        or (outputType = 3 -- deferred
            and path is null and method is null and hashAlgo is null and hash is null)
        or (outputType = 4 -- impure
            and path is null and hash is null
            and method is not null and hashAlgo is not null)
    )
);

-- Derivation::inputDrvs (top-level DerivedPathMap entries)
--
-- This encodes a member of the set of inputs which is a SingleDerivedPath.
create table if not exists DerivationInputs (
    drv        integer not null,
    -- store path of the input
    path       text not null,
    -- JSON array of output names needed from this input.
    --
    -- The encoding is like this:
    --
    -- []: Opaque(path)
    -- [output0]: Built(Opaque(path), output0)
    -- [output0, output1]: Built(Built(Opaque path, output0), output1)
    -- [..., outputN]: Built(..., outputN)
    outputs    jsonb not null
        check (json_type(outputs) = 'array'),
    primary key (drv, path, outputs),
    foreign key (drv) references Derivations(id) on delete cascade
);

-- BasicDerivation::inputSrcs
create table if not exists DerivationInputSrcs (
    drv        integer not null,
    -- store path
    src        text not null,
    primary key (drv, src),
    foreign key (drv) references Derivations(id) on delete cascade
);

-- BasicDerivation::env
create table if not exists DerivationEnv (
    drv        integer not null,
    key        text not null,
    value      text not null,
    primary key (drv, key),
    foreign key (drv) references Derivations(id) on delete cascade
);

-- BasicDerivation::structuredAttrs (key -> JSON value pairs)
-- Rows can only exist when parent Derivations.hasStructuredAttrs = 1.
-- nullopt vs empty object: if hasStructuredAttrs = 0, no rows possible
-- (FK constraint). If 1, zero rows = empty object {}.
create table if not exists DerivationStructuredAttrs (
    drv                integer not null,
    -- Always 1; enforced by CHECK. Part of composite FK to ensure
    -- parent Derivations row has hasStructuredAttrs = 1.
    --
    -- This is the same pseudo-denormalization trick that we did with
    -- DerivationOutputsV2::outputType.
    hasStructuredAttrs integer not null check (hasStructuredAttrs = 1),
    key                text not null,
    value              jsonb not null,
    primary key (drv, hasStructuredAttrs, key),
    foreign key (drv, hasStructuredAttrs)
        references Derivations(id, hasStructuredAttrs) on delete cascade
)
