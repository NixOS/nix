-- Extension of the sql schema for content-addressed derivations.
-- Won't be loaded unless the experimental feature `ca-derivations`
-- is enabled

create table if not exists Realisations (
    id integer primary key autoincrement not null,
    drvPath text not null,
    outputName text not null, -- symbolic output id, usually "out"
    outputPath integer not null,
    signatures text, -- space-separated list
    foreign key (outputPath) references ValidPaths(id) on delete cascade
);

create index if not exists IndexRealisations on Realisations(drvPath, outputName);

-- We can end-up in a weird edge-case where a path depends on itself because
-- it’s an output of a CA derivation, that happens to be the same as one of its
-- dependencies.
-- In that case we have a dependency loop (path -> realisation1 -> realisation2
-- -> path) that we need to break by removing the dependencies between the
-- realisations
create trigger if not exists DeleteSelfRefsViaRealisations before delete on ValidPaths
  begin
    delete from RealisationsRefs where realisationReference = (
      select id from Realisations where outputPath = old.id
    );
  end;

create table if not exists RealisationsRefs (
    referrer integer not null,
    realisationReference integer,
    foreign key (referrer) references Realisations(id) on delete cascade,
    foreign key (realisationReference) references Realisations(id) on delete restrict
);

-- used by QueryRealisationReferences
create index if not exists IndexRealisationsRefs on RealisationsRefs(referrer);
-- used by cascade deletion when ValidPaths is deleted
create index if not exists IndexRealisationsRefsOnOutputPath on Realisations(outputPath);
