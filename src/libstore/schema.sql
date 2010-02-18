pragma foreign_keys = on;

create table if not exists ValidPaths (
    path             text primary key not null,
    hash             text not null,
    registrationTime integer not null
);

create table if not exists Refs (
    referrer  text not null,
    reference text not null,
    primary key (referrer, reference),
    foreign key (referrer) references ValidPaths(path)
      on delete cascade
      deferrable initially deferred,
    foreign key (reference) references ValidPaths(path)
      on delete restrict
      deferrable initially deferred
);

create table if not exists FailedDerivations (
    path text primary key not null,
    time integer not null
);

create index IndexReferrer on Refs(referrer);
create index IndexReference on Refs(reference);
