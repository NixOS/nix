# RFC 7159, section 13.
builtins.fromJSON
  ''
    {
      "Video": {
          "Title":  "The Penguin Chronicles",
          "Width":  1920,
          "Height": 1080,
          "EmbeddedData": [3.1415927, 23493,null, true  ,false, -10],
          "Thumb": {
              "Url":    "http://www.example.com/image/481989943",
              "Width":  200
              "Height": 255,
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
      EmbeddedData = [ 3.1415927 23493 null true false (0-10) ];
      Thumb =
        { Url = http://www.example.com/image/481989943;
          Width = 200;
          Height = 250;
        };
      Subtitle = false;
      Latitude = 46.2051;
      Longitude = 6.0723;
    };
  }
