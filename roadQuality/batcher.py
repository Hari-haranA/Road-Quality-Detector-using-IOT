import time
import socket
import struct
import datetime
from google.cloud import firestore
import firebase_admin
from firebase_admin import credentials
from firebase_admin import firestore



# load the firebase credentials from a local file

cred = credentials.Certificate('firebaseAdminCredentials.json')
firebase_admin.initialize_app(cred)

# get a connection to the firebase firestore database

db = firestore.client()


# create a TCP socket that will listen for a connection from the esp8266

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', 5000))

# begin listening and waiting for a connection

sock.listen()
while True:
	try:
		while True:
			print('waiting for connection')
			conn, addr = sock.accept()

			# read the deviceID and number of records from the Argon

			deviceID = conn.recv(24).decode('utf-8')
			numReadings = struct.unpack('<H', conn.recv(2))[0]
			print(f'deviceID: {deviceID}, readings: {numReadings}')
			
			# read all of the binary data for the sensorReading packets

			readings = []
			for i in range(numReadings):

				# each packet is 16 bytes long
				buf = conn.recv(16)

				# unpack the values into time, latitude, longitude, and accelerationZ
				t, lat, lng, accz = struct.unpack('<iiif', buf)
				t = datetime.datetime.fromtimestamp(t + 946684800)	# the GPS library on the esp8266 gives time in seconds since 2000 rather than typical unix time since 1970
				lat /= 1e7
				lng /= 1e7

				# add all of the received readings to a list to be uploaded later
				print(t.strftime('%Y-%m-%dT%H:%M:%SZ'), lat, lng, accz)
				readings.append((t, lat, lng, accz))
			
			# close the connection so the Argon is not waiting for Python to upload all of the readings
			conn.close()
			

			# add all of the records in a batch to make upload speed faster
			print('batching')
			batch = db.batch()
			for reading in readings:
				t, lat, lng, accz = reading
				datapoint_ref = db.collection('datapoints').document()		# get the reference to a new document in the 'datapoints' collection
				
				# set the contents of the document to the data from the reading
				batch.set(datapoint_ref, {
					'device': deviceID,
					'time': t,
					'location': firestore.GeoPoint(lat, lng),
					'acceleration': accz
				})

			# commit the batch to start the upload to the Firestore Database
			batch.commit()
			print('done!')

	except KeyboardInterrupt: # Ctrl+Break (Windows) will close the program if necessary
		break



