# RFC 7159, section 13.
builtins.fromJSON
  ''
    {
      "Image": {
          "Width":  800,
          "Height": 600,
          "Title":  "View from 15th Floor",
          "Thumbnail": {
              "Url":    "http://www.example.com/image/481989943",
              "Height": 125,
              "Width":  100
          },
          "Animated" : false,
          "IDs": [116, 943, 234, 38793, true  ,false,null, -100],
          "Escapes": "\"\\\/\t\n\r\t"
        }
    }
  ''
==
  { Image =
    { Width = 800;
      Height = 600;
      Title = "View from 15th Floor";
      Thumbnail =
        { Url = http://www.example.com/image/481989943;
          Height = 125;
          Width = 100;
        };
      Animated = false;
      IDs = [ 116 943 234 38793 true false null (0-100) ];
      Escapes = "\"\\\/\t\n\r\t";  # supported in JSON but not Nix: \b\f
    };
  }
