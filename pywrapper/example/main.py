import os
import threading
import time
import torch
import torch.distributed.autograd as dist_autograd
import torch.multiprocessing as mp
import torch.nn as nn
import torchvision

from pyserf import PySerfXORCompressor, PySerfXORDecompressor
from torch import optim
from torch.distributed import rpc
from torch.distributed.optim import DistributedOptimizer
from torch.distributed.rpc import RRef
from torchvision.transforms import transforms


COMPRESS_ERROR_BOUND = 0.0001
GLOBAL_DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")
GLOBAL_DEVICE = torch.device("cpu")
GLOBAL_EPOCHS = 1
torch.set_default_dtype(torch.float64)
torch.set_printoptions(precision=10)



compressor = PySerfXORCompressor(1000, COMPRESS_ERROR_BOUND, 0)
decompressor = PySerfXORDecompressor(0)



class ConvNetBase(nn.Module):
    def __init__(self):
        super(ConvNetBase, self).__init__()
        self._lock = threading.Lock()

    def parameter_rrefs(self):
        return [RRef(p) for p in self.parameters()]


class ConvNetShard1(ConvNetBase):
    def __init__(self, device):
        super(ConvNetShard1, self).__init__()
        self.device = device
        self.seq = nn.Sequential(
            nn.Conv2d(1, 16, kernel_size=5, stride=1, padding=2),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(kernel_size=2, stride=2),
        ).to(self.device)
        # self.compressor = PySerfXORCompressor(1000, COMPRESS_ERROR_BOUND, 0)

    def forward(self, x_rref):
        x = x_rref.to_here().to(self.device)
        with self._lock:
            out = self.seq(x).to("cpu")
        pack = []
        counter = 0
        for element in out.view(-1):
            compressor.add_value(element)
            counter += 1
            if counter == 900:
                compressor.close()
                pack.append(compressor.get())
                counter = 0
        if counter != 0:
            compressor.close()
            pack.append(compressor.get())
        return (pack, x.shape)


class ConvNetShard2(ConvNetBase):
    def __init__(self, device):
        super(ConvNetShard2, self).__init__()
        self.device = device
        self.seq = nn.Sequential(
            nn.Conv2d(16, 32, kernel_size=5, stride=1, padding=2),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.MaxPool2d(kernel_size=2, stride=2),
        ).to(self.device)
        self.fc = nn.Linear(7 * 7 * 32, 10)
        # self.decompressor = PySerfXORDecompressor(0)

    def forward(self, x_rref):
        compressed_pack, shape = x_rref.to_here()
        decompressed_values = []
        for pack in compressed_pack:
            decompressed_values.extend(decompressor.decompress(pack))
        x = torch.tensor(decompressed_values).view(shape).to(self.device)
        with self._lock:
            out = self.seq(x)
            out = out.reshape(out.size(0), -1)
            out = self.fc(out)
        return out


class DistConvNet(nn.Module):
    def __init__(self, workers, *args, **kwargs):
        super(DistConvNet, self).__init__()

        self.layer1_rref = rpc.remote(
            workers[0],
            ConvNetShard1,
            args=(GLOBAL_DEVICE,),
            kwargs=kwargs,
        )

        self.layer2_rref = rpc.remote(
            workers[1],
            ConvNetShard2,
            args=(GLOBAL_DEVICE,),
            kwargs=kwargs,
        )

    def forward(self, x):
        x_rref = RRef(x)
        y_rref = self.layer1_rref.remote().forward(x_rref)
        z_fut = self.layer2_rref.rpc_async().forward(y_rref)
        return z_fut

    def parameter_rrefs(self):
        remote_params = []
        remote_params.extend(
            self.layer1_rref.remote().parameter_rrefs().to_here())
        remote_params.extend(
            self.layer2_rref.remote().parameter_rrefs().to_here())
        return remote_params


def run_master():
    # Load data

    train_dataset = torchvision.datasets.MNIST(
        root="./data", train=True, transform=transforms.ToTensor(), download=True
    )

    training_loader = torch.utils.data.DataLoader(
        train_dataset, batch_size=10, shuffle=True
    )

    test_dataset = torchvision.datasets.MNIST(
        root="./data", train=False, transform=transforms.ToTensor(), download=True
    )

    testing_loader = torch.utils.data.DataLoader(
        test_dataset, batch_size=10, shuffle=True
    )

    # put the two model parts on worker1 and worker2 respectively

    model = DistConvNet(["worker1", "worker2"])

    # Choose loss function and optimizer for backward

    loss_fn = nn.CrossEntropyLoss()
    optimizer = DistributedOptimizer(
        optim.SGD, model.parameter_rrefs(), lr=0.001, momentum=0.9
    )
    # Start training
    for e in range(GLOBAL_EPOCHS):
        for batch_idx, (data, target) in enumerate(training_loader):
            data, target = data.to("cpu"), target.to(GLOBAL_DEVICE)
            with dist_autograd.context() as context:
                output = model(data)
                output.wait()
                dist_autograd.backward(
                    context, [loss_fn(output.value(), target)])
                optimizer.step(context)
        print(f"Epoch {e + 1}/{GLOBAL_EPOCHS}")
    total_test_loss = 0
    total_accuracy = 0
    model.eval()

    with torch.no_grad():
        for data, target in testing_loader:
            output = model(data)
            output.wait()
            loss = loss_fn(output.value(), target)
            total_test_loss += loss.item()
            accuracy_ = (output.value().argmax(1) == target).sum()
            total_accuracy += accuracy_
    print(
        "Loss: {:.4f}, acc: {:.2f}%".format(
            total_test_loss,
            (total_accuracy.float() / len(test_dataset)) * 100,
        )
    )


def run_worker(rank, world_size):
    os.environ["MASTER_ADDR"] = "localhost"
    os.environ["MASTER_PORT"] = "29500"
    options = rpc.TensorPipeRpcBackendOptions(num_worker_threads=128)

    if rank == 0:
        print(f"Starting master...")
        rpc.init_rpc(
            "master", rank=rank, world_size=world_size, rpc_backend_options=options
        )
        run_master()
    else:
        print(f"Starting worker{rank} ...")
        rpc.init_rpc(
            f"worker{rank}",
            rank=rank,
            world_size=world_size,
            rpc_backend_options=options,
        )
    # block until all rpcs finish

    rpc.shutdown()


if __name__ == "__main__":
    time_start = time.time()
    world_size = 3
    mp.spawn(run_worker, args=(world_size,), nprocs=world_size, join=True)
    time_end = time.time()
    print("train time is ", time_end - time_start)
