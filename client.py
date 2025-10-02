import socket
import threading
import json
import struct
import os
import time

running = True
request_recv = 0
request_send = 0
recieving_file = False

def get_json_data(client):
    output = ""
    while not output.endswith("\n"):
        byte = client.recv(1)

        if byte == 0: return False

        output += byte.decode("utf-8")
    
    output.replace("\n", "")

    return output

def handle_rec(client):
    global running
    global recieving_file
    global request_recv
    global request_send

    while(running):
        result = get_json_data(client)
        file_name = ""
        file_size = 0
        total_file_size = 0
        current_file_size = 0

        if not result:
            break

        json_data = json.loads(result)

        if json_data.get("type") == "request":
            print(json_data.get("message"))
            print("\ruse the command accept to recieve the file or reject to deny")
            print("Enter command: ")
            request_recv = json_data.get("from")
        elif json_data.get("type") == "accept":
            request_send = json_data.get("from")
            print("\r" + json_data.get("message"))
            print("Enter command: ")
        elif json_data.get("type") == "file_metadata":
            print("Creating File: " + json_data.get("file_name"))
            file_name = json_data.get("file_name")
            file_size = json_data.get("file_size")
            total_file_size = file_size
            recieving_file = True

            os.makedirs("Download", exist_ok=True)

            with open("Download/" + file_name, "wb") as f:
                finished_downloading = False
                byte_chunk_size = 1024

                while (not finished_downloading):
                    chunck_size = byte_chunk_size

                    if current_file_size + chunck_size > file_size:
                        chunck_size = file_size - current_file_size
                    
                    server_message = client.recv(chunck_size)
                    f.write(server_message)

                    current_file_size += len(server_message)

                    print(f"File Downloading: Filesize {current_file_size} / {file_size}")

                    if current_file_size >= file_size:
                        finished_downloading = True
                        recieving_file = False
                        # print(f"File Downloading Complete: Filesize {current_file_size} / {file_size}")
        elif json_data.get("type") == "file_transfer_finished":
            print(json_data.get("message"))



client = socket.create_connection(("localhost", 3000))

data = client.recv(4)

client_id = struct.unpack("i", data)[0]

print(f"Connected to server\nClient id: {client_id}")

threading.Thread(target=handle_rec, args=(client,), daemon=True).start()

while(running):
    if recieving_file:
        time.sleep(1)
        continue

    command = input("Enter command: ")

    if recieving_file:
        time.sleep(1)
        continue

    cmd = command
    arguments = []
    if " " in command:
        cmd = command.split(" ")[0]
        arguments = command.split(" ")
        arguments.remove(command.split(" ")[0])
    
    if cmd == "request":
        if len(arguments) > 0:
            print(f"Sending a request to client {arguments[0]}.")
            client.sendall((json.dumps({"type": "request", "target": int(arguments[0])}) + "\n").encode("utf-8"))
        else:
            target = input("Enter request target: ")
            print(f"Sending a request to client {target}.")
            client.sendall((json.dumps({"type": "request", "target": int(target)}) + "\n").encode("utf-8"))
    elif cmd == "exit":
        running = False
    elif cmd == "accept" and request_recv != 0:
        client.send((json.dumps({"type": "accept", "to": request_recv}) + "\n").encode("utf-8"))
    elif cmd == "reject" and request_recv != 0:
        client.send((json.dumps({"type": "reject", "to": request_recv}) + "\n").encode("utf-8"))
    elif cmd == "send" and request_send != 0:
        file_path = ""
        if len(arguments) > 0:
            file_path = arguments[0]
        else:
            file_path = input("Enter File path: ")
        
        file_name = file_path.split("/")[-1]
        file_size = os.stat(file_path).st_size

        client.sendall((json.dumps({"type": "file_metadata", "file_name": file_name, "file_size": file_size, "to": request_send}) + "\n").encode("utf-8"))

        with open(file_path, "rb") as f:
            chunck_size = 1024

            while True:
                chunk = f.read(chunck_size)
                if not chunk: break

                client.sendall(chunk)

        client.sendall((json.dumps({
            "type": "file_transfer_finished",
            "message": f"Finished sending {file_name}",
            "to": request_send
        }) + "\n").encode("utf-8"))

client.close()
