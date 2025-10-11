const char *HEAD = R"(
    <!DOCTYPE html>
    <html lang="en">
      <head>
        <meta charset="UTF-8" />
        <meta name="viewport" content="width=device-width, initial-scale=1.0" />
        <title>Devices Status</title>
        <!-- Bootstrap CSS -->
        <link
          href="https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css"
          rel="stylesheet"
        />
        <style>
          /* Custom CSS */
          .box {
            border: 3px solid #ccc;
            padding: 20px;
            text-align: center;
            margin-bottom: 10px;
            width: 200px;
          }
          /* Green background for online boxes */
          .box.online {
            background-color: #4caf50; /* Green */
            color: white;
          }
          /* Red background for offline boxes */
          .box.offline {
            background-color: #f73109; /* Red */
            color: white;
          }
          .box.prevonline {
              background-color: #e2dc26; /* Yellow */
              color: white;
          }
        </style>
      </head>

      <body>
        <div class="container-fluid text-center">
            <div class="row">
)";

const char *TAIL = R"(
            </div>
        </div>
      </body>
    </html>
)";


// onlineBox ( ip, MAC, Vendor )
const char *CHAR_ONLINE_BOX = "<div class=\"box online\"> <div> %s <br> %s <br> %s </div> </div>";

// offlineBox
const char *CHAR_OFFLINE_BOX = "<div class=\"box offline\"> <div> %s <br> <br> <br> </div> </div>";

// prev online ( ip, MAC, Vendor )
const char *CHAR_PREVONLINE_BOX = "<div class=\"box prevonline\"> <div> %s <br> %s <br> %s </div> </div>"
