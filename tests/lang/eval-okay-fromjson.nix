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
          "Subtitle" : false,
          "Latitude":  46.2051,
          "Longitude": 6.0723
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
      Subtitle = false;
      Latitude = 46.2051;
      Longitude = 6.0723;
    };
  }
