module.exports = [
  {
    "type": "heading",
    "defaultValue": "GlanceWeather Configuration"
  },
  {
    "type": "text",
    "defaultValue": "Update the configuration below."
  },
  {
    "type": "section",
    "defaultValue": "How the watch responds to wrist movements",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Glance Behaviour"
      },
      {
        "type": "toggle",
        "messageKey": "CfgBacklight",
        "defaultValue": true,
        "label": "Backlight when active"
      },
      {
        "type": "toggle",
        "messageKey": "CfgFlickBacklight",
        "defaultValue": true,
        "label": "Flick backlight support"
      },
      {
        "type": "slider",
        "messageKey": "CfgLightTime",
        "defaultValue": "5",
        "label": "Light time (s)",
        "min": 1,
        "max": 30
      },
      {
        "type": "slider",
        "messageKey": "CfgActiveTime",
        "defaultValue": "30",
        "label": "Active timeout (s)",
        "min": 1,
        "max": 60
      },
      {
        "type": "slider",
        "messageKey": "CfgRollTime",
        "defaultValue": "1000",
        "label": "Light time (ms)",
        "min": 300,
        "max": 5000,
        "step": 200
      },
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Weather Source"
      },
      {
        "type": "input",
        "messageKey": "CfgApiKey",
        "defaultValue": "",
        "label": "ForecastIO API Key",
        "attributes": {
          "limit": 32
        }
      }      
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];