builtins.fromJSON
  ''
    {
      "Video": {
          "Title":  "The Penguin Chronicles",
          "Width":  1920,
          "Height": 1080,
          "EmbeddedData": [3.14159, 23493,null, true  ,false, -10],
          "Thumb": {
              "Url":    "http://www.example.com/video/5678931",
              "Width":  200,
              "Height": 250
          },
          "Animated" : false,
          "IDs": [116, 943, 234, 38793, true  ,false,null, -100],
          "Escapes": "\"\\\/\t\n\r\t",
          "Subtitle" : false,
          "Latitude":  37.7668,
          "Longitude": -122.3959
        }
    }
  ''
==
  { Video =
    { Title = "The Penguin Chronicles";
      Width = 1920;
      Height = 1080;
      EmbeddedData = [ 3.14159 23493 null true false (0-10) ];
      Thumb =
        { Url = "http://www.example.com/video/5678931";
          Width = 200;
          Height = 250;
        };
      Animated = false;
      IDs = [ 116 943 234 38793 true false null (0-100) ];
      Escapes = "\"\\\/\t\n\r\t";  # supported in JSON but not Nix: \b\f
      Subtitle = false;
      Latitude = 37.7668;
      Longitude = -122.3959;
    };
  }
