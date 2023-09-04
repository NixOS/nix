-- Extension of the sql schema for content-addressed derivations.
-- Won't be loaded unless the experimental feature `ca-derivations`
-- is enabled

create table if not exists Realisations (
    drvPath text not null,
    outputName text not null, -- symbolic output id, usually "out"
    outputPath integer not null,
    signatures text, -- space-separated list
    primary key (drvPath, outputName),
    foreign key (outputPath) references ValidPaths(id) on delete cascade
);
