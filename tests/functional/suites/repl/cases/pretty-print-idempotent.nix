{
  oneDeep = {
    homepage = "https://" + "example.com";
  };
  twoDeep = {
    layerOne = {
      homepage = "https://" + "example.com";
    };
  };

  oneDeepList = [
    ("https://" + "example.com")
  ];
  twoDeepList = [
    [
      ("https://" + "example.com")
    ]
  ];
}
