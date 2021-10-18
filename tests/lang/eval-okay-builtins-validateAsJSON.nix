let
  schema = {
    title = "A person";
    properties = {
      age = {
        description = "Age of the person";
        type = "number";
        minimum = 1;
        maximum = 200;
      };
      name = {
        description = "Complete Name for the person";
        first.type = "string";
        last.type = "string";
        required = [ "first" "last" ];
        type = "object";
      };
    };
    required = [ "name" "age" ];
    type = "object";
  };
in
[
  (validateAsJSON "Invalid JSON schema" { })
  (validateAsJSON schema { })
  (validateAsJSON schema { age = 24; name.first = "Jane"; })
  (validateAsJSON schema { age = 24; name.first = "Jane"; name.last = "Doe"; })
]
